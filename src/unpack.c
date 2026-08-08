#include "image_format.h"
#include "log.h"
#include "manifest.h"

#include <stdbool.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

#define UNPACK_PATH_SIZE 4096U

typedef struct {
  uint8_t        *buffer;
  size_t          size;
  size_t          headerOffset;
  const Manifest *manifest;
} PackedImage;

static bool
join_path(char *result, size_t size, const char *directory, const char *name)
{
  int written = snprintf(result, size, "%s/%s", directory, name);
  return written >= 0 && (size_t)written < size;
}

static bool make_directory(const char *path)
{
  if (mkdir(path, 0777) == 0)
    return true;
  return errno == EEXIST;
}

static bool write_buffer(const char *path, const uint8_t *buffer, size_t size)
{
  FileContent file = {
      .filePath = path, .fileBuffer = (uint8_t *)buffer, .fileSize = size};
  return write_file_content(&file) == 0;
}

static bool range_inside(size_t offset, size_t size, size_t limit)
{
  return offset <= limit && size <= limit - offset;
}

static bool ranges_overlap(
    size_t firstOffset, size_t firstSize, size_t secondOffset,
    size_t secondSize)
{
  return firstOffset < secondOffset + secondSize &&
         secondOffset < firstOffset + firstSize;
}

static bool write_qc_aboot_header(uint8_t *output, uint64_t size)
{
  if (size > UINT32_MAX)
    return false;
  memcpy(output, "UNCOMPRESSED_IMG", 16);
  image_write_le32(output + 0x10, (uint32_t)size);
  return true;
}

static bool valid_name(const char name[32])
{
  const char *end = memchr(name, '\0', 32);
  if (end == NULL || end == name)
    return false;
  for (const char *current = name; current < end; current++) {
    unsigned char character = (unsigned char)*current;
    /* Reject control characters and ';', which inih treats as an
     * inline comment and would truncate the generated config. */
    if (character < 0x20 || character == 0x7f || character == ';')
      return false;
  }
  return true;
}

static bool load_packed(const char *path, PackedImage *packed)
{
  FileContent file = {.filePath = path};
  if (!get_file_size(&file))
    return false;
  packed->buffer = malloc(file.fileSize);
  if (packed->buffer == NULL)
    return false;
  file.fileBuffer = packed->buffer;
  if (read_file_content(&file) == NULL) {
    free(packed->buffer);
    packed->buffer = NULL;
    return false;
  }
  packed->size = file.fileSize;

  FileContent view = {.fileBuffer = packed->buffer, .fileSize = packed->size};
  Arm64ImageInfo image = {0};
  if (!image_inspect_arm64_linux(&view, &image)) {
    log_err("Input is not an ARM64 Linux Image.");
    return false;
  }
  packed->headerOffset = image.headerOffset;
  if (packed->size - packed->headerOffset < ARM64_LINUX_IMAGE_HEADER_SIZE) {
    log_err("Input image is truncated.");
    return false;
  }

  const uint8_t *header         = packed->buffer + packed->headerOffset;
  uint64_t       manifestOffset = image_read_le64(header + 0x20);
  uint64_t       manifestSize   = image_read_le64(header + 0x28);
  uint64_t       runtimeSize    = image_read_le64(header + 0x10);
  if (manifestOffset > SIZE_MAX || manifestSize > SIZE_MAX ||
      runtimeSize != packed->size - packed->headerOffset ||
      !range_inside(
          (size_t)manifestOffset, (size_t)manifestSize, (size_t)runtimeSize) ||
      manifestSize < sizeof(ManifestHeader)) {
    log_err("Input image has invalid Manifest bounds.");
    return false;
  }

  const uint8_t  *manifestBuffer = header + (size_t)manifestOffset;
  const Manifest *manifest       = (const Manifest *)manifestBuffer;
  if (manifest->header.magic != MANIFEST_MAGIC ||
      manifest->header.version.major != MANIFEST_VERSION_MAJOR ||
      manifest->header.version.minor != MANIFEST_VERSION_MINOR) {
    log_err("Input image is not a supported reversible image.");
    return false;
  }
  if (manifest->header.entryCount == 0 ||
      manifest->header.entryCount > MANIFEST_MAX_ENTRIES ||
      manifest->header.defaultEntry >= manifest->header.entryCount) {
    log_err("Input image has an invalid Manifest.");
    return false;
  }

  size_t entryCount = manifest->header.entryCount;
  size_t expectedManifestSize =
      sizeof(ManifestHeader) + entryCount * sizeof(ManifestEntry);
  if (manifestSize != expectedManifestSize) {
    log_err("Input image has an invalid Manifest size.");
    return false;
  }

  uint64_t primarySlotValue = image_read_le64(header + 0x30);
  if (entryCount < 2 || primarySlotValue == 0 || primarySlotValue > SIZE_MAX) {
    log_err("Input image has invalid Shim bounds.");
    return false;
  }
  size_t               primarySlot       = (size_t)primarySlotValue;
  size_t               attachedShimIndex = entryCount - 1;
  const ManifestEntry *attachedShim = &manifest->entries[attachedShimIndex];
  if (manifest->header.defaultEntry == attachedShimIndex ||
      attachedShim->type != SHIM_ENTRY_TYPE_SHIM ||
      attachedShim->flags != SHIM_ENTRY_FLAG_EXECUTABLE ||
      attachedShim->offset != primarySlot || attachedShim->size == 0 ||
      attachedShim->entryOffset != 0) {
    log_err("Input image has invalid Shim bounds.");
    return false;
  }
  for (size_t index = 0; index < entryCount; index++) {
    const ManifestEntry *entry = &manifest->entries[index];
    if (manifest_entry_type_name(entry->type) == NULL ||
        !valid_name(entry->name) || entry->offset > SIZE_MAX ||
        entry->size > SIZE_MAX ||
        !range_inside(
            (size_t)entry->offset, (size_t)entry->size,
            (size_t)manifestOffset)) {
      log_err("Input image has an invalid Manifest entry.");
      return false;
    }
  }

  packed->manifest = manifest;

  if (manifest->entries[0].offset != 0 ||
      manifest->entries[0].type != SHIM_ENTRY_TYPE_LINUX ||
      (manifest->entries[0].flags &
       (SHIM_ENTRY_FLAG_BASE_IMAGE | SHIM_ENTRY_FLAG_EXECUTABLE)) !=
          (SHIM_ENTRY_FLAG_BASE_IMAGE | SHIM_ENTRY_FLAG_EXECUTABLE) ||
      (manifest->entries[0].flags &
       ~(SHIM_ENTRY_FLAG_BASE_IMAGE | SHIM_ENTRY_FLAG_EXECUTABLE |
         SHIM_ENTRY_FLAG_HAS_DEVICE_TREE_BLOB)) != 0 ||
      manifest->entries[0].size < ARM64_LINUX_IMAGE_HEADER_SIZE)
    return false;
  size_t attachedShimEnd =
      (size_t)attachedShim->offset + (size_t)attachedShim->size;
  for (size_t index = 0; index < entryCount; index++) {
    const ManifestEntry *entry = &manifest->entries[index];
    uint32_t             allowedFlags =
        index == 0 ? SHIM_ENTRY_FLAG_BASE_IMAGE | SHIM_ENTRY_FLAG_EXECUTABLE |
                         SHIM_ENTRY_FLAG_HAS_DEVICE_TREE_BLOB
                   : SHIM_ENTRY_FLAG_COPY | SHIM_ENTRY_FLAG_EXECUTABLE |
                         SHIM_ENTRY_FLAG_HAS_DEVICE_TREE_BLOB |
                         SHIM_ENTRY_FLAG_HAS_QC_ABOOT_HEADER;
    bool copyImage = (entry->flags & SHIM_ENTRY_FLAG_COPY) != 0;
    bool executable = (entry->flags & SHIM_ENTRY_FLAG_EXECUTABLE) != 0;
    bool hasDtb = (entry->flags & SHIM_ENTRY_FLAG_HAS_DEVICE_TREE_BLOB) != 0;
    bool hasQcAboot =
        (entry->flags & SHIM_ENTRY_FLAG_HAS_QC_ABOOT_HEADER) != 0;
    if ((entry->flags & ~allowedFlags) != 0 || entry->alignment < 2 ||
        entry->alignment > 31 ||
        (hasQcAboot && entry->type != SHIM_ENTRY_TYPE_LINUX) ||
        (copyImage && (entry->loadAddress == 0 || entry->copySizeMax == 0 ||
                       entry->size > entry->copySizeMax)) ||
        (!copyImage && entry->copySizeMax != 0)) {
      log_err("Input image has invalid Manifest metadata.");
      return false;
    }
    if (executable != manifest_entry_type_is_executable(entry->type)) {
      log_err("Input image has inconsistent executable metadata.");
      return false;
    }
    if (!hasDtb && entry->deviceTreeBlobEntry != 0) {
      log_err(
          "Input image has a stale DeviceTreeBlob "
          "reference.");
      return false;
    }
    if (hasDtb) {
      if (!executable || entry->deviceTreeBlobEntry >= entryCount ||
          manifest->entries[entry->deviceTreeBlobEntry].type !=
              SHIM_ENTRY_TYPE_DTB ||
          manifest->entries[entry->deviceTreeBlobEntry].flags != 0) {
        log_err(
            "Input image has an invalid DeviceTreeBlob "
            "reference.");
        return false;
      }
    }
    if (index == 0) {
      if (entry->size > primarySlot)
        return false;
      continue;
    }
    for (size_t previous = 1; previous < index; previous++) {
      const ManifestEntry *other = &manifest->entries[previous];
      if (ranges_overlap(
              (size_t)entry->offset, (size_t)entry->size, (size_t)other->offset,
              (size_t)other->size)) {
        log_err("Manifest entries overlap.");
        return false;
      }
    }
    if (index != attachedShimIndex && (size_t)entry->offset < attachedShimEnd)
      return false;
  }
  return true;
}

static bool restore_files(const PackedImage *packed, const char *directory)
{
  char                 path[UNPACK_PATH_SIZE * 2];
  const Manifest      *manifest          = packed->manifest;
  size_t               entryCount        = manifest->header.entryCount;
  size_t               attachedShimIndex = entryCount - 1;
  const ManifestEntry *attachedShim = &manifest->entries[attachedShimIndex];

  FileContent base = {0};
  base.fileSize    = (size_t)manifest->entries[0].size;
  base.fileBuffer  = malloc(base.fileSize);
  if (base.fileBuffer == NULL)
    return false;
  memcpy(base.fileBuffer, packed->buffer + packed->headerOffset, base.fileSize);
  image_write_le64(base.fileBuffer, manifest->header.baseImageInstructions);
  image_write_le64(base.fileBuffer + 0x10, manifest->header.baseImageImageSize);
  image_write_le64(
      base.fileBuffer + 0x20, manifest->header.baseImageManifestOffset);
  image_write_le64(
      base.fileBuffer + 0x28, manifest->header.baseImageManifestSize);
  image_write_le64(
      base.fileBuffer + 0x30, manifest->header.baseImageShimOffset);
  {
    const ManifestEntry *baseEntry = &manifest->entries[0];
    uint8_t              prefix[QC_ABOOT_KERNEL_HDR_SIZE];
    size_t               prefixSize = packed->headerOffset;
    if (prefixSize > sizeof(prefix) ||
        (prefixSize != 0 && baseEntry->size > UINT32_MAX) ||
        !join_path(path, sizeof(path), directory, "BaseImage") ||
        prefixSize + base.fileSize > SIZE_MAX) {
      free(base.fileBuffer);
      return false;
    }
    if (prefixSize != 0) {
      memcpy(prefix, packed->buffer, prefixSize);
      image_write_le32(prefix + 0x10, (uint32_t)baseEntry->size);
    }
    size_t   restoredSize = prefixSize + base.fileSize;
    uint8_t *restored     = malloc(restoredSize);
    if (restored == NULL) {
      free(base.fileBuffer);
      return false;
    }
    memcpy(restored, prefix, prefixSize);
    memcpy(restored + prefixSize, base.fileBuffer, base.fileSize);
    bool result = write_buffer(path, restored, restoredSize);
    free(restored);
    free(base.fileBuffer);
    if (!result)
      return false;
  }

  if (!join_path(path, sizeof(path), directory, "Shim.bin") ||
      !write_buffer(
          path, packed->buffer + packed->headerOffset + attachedShim->offset,
          (size_t)attachedShim->size))
    return false;

  for (size_t index = 1; index < attachedShimIndex; index++) {
    const ManifestEntry *entry    = &manifest->entries[index];
    const char          *typeName = manifest_entry_type_name(entry->type);
    char                 name[64];
    snprintf(name, sizeof(name), "Image-%02zu-%s", index, typeName);
    if (!join_path(path, sizeof(path), directory, name))
      return false;
    uint8_t prefix[QC_ABOOT_KERNEL_HDR_SIZE];
    size_t  prefixSize = 0;
    if (entry->flags & SHIM_ENTRY_FLAG_HAS_QC_ABOOT_HEADER) {
      if (!write_qc_aboot_header(prefix, entry->size))
        return false;
      prefixSize = QC_ABOOT_KERNEL_HDR_SIZE;
    }
    size_t   outputSize = prefixSize + (size_t)entry->size;
    uint8_t *output     = malloc(outputSize);
    if (output == NULL)
      return false;
    memcpy(output, prefix, prefixSize);
    memcpy(
        output + prefixSize,
        packed->buffer + packed->headerOffset + entry->offset,
        (size_t)entry->size);
    bool result = write_buffer(path, output, outputSize);
    free(output);
    if (!result)
      return false;
  }
  return true;
}

static bool write_config(const PackedImage *packed, const char *directory)
{
  char path[UNPACK_PATH_SIZE * 2];
  if (!join_path(path, sizeof(path), directory, "config.cfg"))
    return false;
  FILE *file = fopen(path, "w");
  if (file == NULL)
    return false;

  const Manifest *manifest = packed->manifest;
  fprintf(file, "[Pack]\nShim=Shim.bin\nOutput=RepackedKernel\n");
  fprintf(
      file, "Default=Image-%02u\nTimeout=%u\n\n", manifest->header.defaultEntry,
      manifest->header.timeoutMs);
  for (size_t index = 0; index + 1 < manifest->header.entryCount; index++) {
    const ManifestEntry *entry    = &manifest->entries[index];
    const char          *typeName = manifest_entry_type_name(entry->type);
    fprintf(file, "[Image-%02zu]\nName=%s\n", index, entry->name);
    if (index == 0)
      fprintf(file, "Path=BaseImage\n");
    else {
      char name[64];
      snprintf(name, sizeof(name), "Image-%02zu-%s", index, typeName);
      fprintf(file, "Path=%s\n", name);
    }
    fprintf(
        file, "Type=%s\nBaseImage=%s\nAlign=%llu\n", typeName,
        index == 0 ? "true" : "false", (unsigned long long)entry->alignment);
    if (entry->flags & SHIM_ENTRY_FLAG_COPY)
      fprintf(
          file, "CopyTo=0x%llx\nCopySizeMax=0x%llx\n",
          (unsigned long long)entry->loadAddress,
          (unsigned long long)entry->copySizeMax);
    if (entry->flags & SHIM_ENTRY_FLAG_EXECUTABLE)
      fprintf(
          file, "EntryOffset=0x%llx\n", (unsigned long long)entry->entryOffset);
    if (entry->flags & SHIM_ENTRY_FLAG_HAS_DEVICE_TREE_BLOB)
      fprintf(
          file, "DeviceTreeBlob=Image-%02llu\n",
          (unsigned long long)entry->deviceTreeBlobEntry);
    fprintf(file, "\n");
  }
  fprintf(file, "[Manifest]\nType=Manifest\n");
  return fclose(file) == 0;
}

int main(int argc, char **argv)
{
  if (argc != 3) {
    log_err("Usage: UnpackTool <PatchedKernel> <OutputDir>");
    return EINVAL;
  }
  PackedImage packed = {0};
  if (!load_packed(argv[1], &packed)) {
    free(packed.buffer);
    return EINVAL;
  }
  if (!make_directory(argv[2]) || !restore_files(&packed, argv[2]) ||
      !write_config(&packed, argv[2])) {
    log_err("Failed to write unpacked files.");
    free(packed.buffer);
    return EINVAL;
  }
  log_ok("Kernel image unpacked successfully.");
  free(packed.buffer);
  return 0;
}