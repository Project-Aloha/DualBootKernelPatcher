#include "pack.h"
#include "image_format.h"
#include "log.h"
#include "utils.h"

#include <stdbool.h>

static bool align_linux_offset(
    size_t minimum, uint64_t primaryTextOffset, uint64_t imageTextOffset,
    size_t alignment, size_t *result)
{
  const size_t mask = alignment - 1;
  size_t       requiredResidue =
      ((size_t)imageTextOffset - (size_t)primaryTextOffset) & mask;
  size_t adjustment = (requiredResidue - (minimum & mask)) & mask;
  if (minimum > SIZE_MAX - adjustment)
    return false;
  *result = minimum + adjustment;
  return (*result & (alignment - 1)) == 0;
}

static bool load_file(FileContent *file)
{
  if (!get_file_size(file))
    return false;
  file->fileBuffer = malloc(file->fileSize);
  return file->fileBuffer != NULL && read_file_content(file) != NULL;
}

static bool resize_output(FileContent *output, size_t newSize)
{
  uint8_t *resized = realloc(output->fileBuffer, newSize);
  if (resized == NULL)
    return false;
  if (newSize > output->fileSize)
    memset(resized + output->fileSize, 0, newSize - output->fileSize);
  output->fileBuffer = resized;
  output->fileSize   = newSize;
  return true;
}

static bool manifest_entry_size(const char *name, size_t *size)
{
  size_t nameLen = strlen(name);
  if (nameLen == 0 || nameLen > UINT32_MAX ||
      nameLen > SIZE_MAX - sizeof(ManifestEntry))
    return false;
  return image_align_up(
      sizeof(ManifestEntry) + nameLen, MANIFEST_ENTRY_ALIGNMENT, size);
}

static ManifestEntry *write_manifest_entry(uint8_t **cursor, const char *name)
{
  size_t nameLen    = strlen(name);
  size_t recordSize = 0;
  if (!manifest_entry_size(name, &recordSize))
    return NULL;

  ManifestEntry *entry = (ManifestEntry *)*cursor;
  entry->nameLen       = (uint32_t)nameLen;
  memcpy(entry->name, name, nameLen);
  *cursor += recordSize;
  return entry;
}

static bool
inspect_typed_blob(const FileContent *file, const PackImageConfig *image)
{
  if (image->type == MF_ENTRY_TYPE_DTB) {
    static const uint8_t fdtMagic[] = {0xd0, 0x0d, 0xfe, 0xed};
    if (file->fileSize < sizeof(fdtMagic) ||
        memcmp(file->fileBuffer, fdtMagic, sizeof(fdtMagic)) != 0) {
      log_err("[%s] is not a flattened device tree blob.", image->section);
      return false;
    }
  }
  return true;
}

static bool patch_kernel_entry(uint8_t *kernelHeader, size_t shimOffset)
{
  if ((shimOffset & (ARM64_BRANCH_ALIGNMENT - 1)) != 0 ||
      shimOffset >= (size_t)ARM64_BRANCH_MAX_OFFSET) {
    log_err("Shim is outside the ARM64 BL range.");
    return false;
  }

  uint32_t firstInstruction  = image_read_le32(kernelHeader);
  uint32_t secondInstruction = image_read_le32(kernelHeader + 4);
  bool     firstIsBranch     = (firstInstruction & 0xfc000000U) == 0x14000000U;
  bool     secondIsBranch    = (secondInstruction & 0xfc000000U) == 0x14000000U;

  if (firstIsBranch && !secondIsBranch) {
    uint32_t movedBranch =
        0x14000000U | ((firstInstruction - 1U) & 0x03ffffffU);
    image_write_le32(kernelHeader + 4, movedBranch);
  }
  else if (!secondIsBranch) {
    log_err("Base image has no supported entry branch.");
    return false;
  }

  uint32_t branchImmediate = (uint32_t)(shimOffset >> 2);
  image_write_le32(kernelHeader, 0x94000000U | branchImmediate);
  return true;
}

int PackImage(const PackConfig *config)
{
  const PackImageConfig *baseImage = &config->images[config->baseImageIndex];
  bool                   writeManifest = config->manifest.present;
  int                    status        = -EINVAL;

  FileContent primary  = {.filePath = baseImage->path};
  FileContent shim     = {.filePath = config->shim};
  FileContent output   = {.filePath = config->output};
  Manifest   *manifest = NULL;

  if (!load_file(&primary) || !load_file(&shim))
    goto cleanup;
  if (shim.fileSize < ARM64_BRANCH_ALIGNMENT) {
    log_err("Shim must contain at least one ARM64 instruction.");
    goto cleanup;
  }

  Arm64ImageInfo primaryInfo = {0};
  if (!image_inspect_arm64_linux(&primary, &primaryInfo)) {
    log_err("Base image is not an ARM64 Linux Image.");
    goto cleanup;
  }

  size_t primarySlot = primaryInfo.payloadSize;
  if (primaryInfo.imageSize > primarySlot) {
    if (primaryInfo.imageSize > SIZE_MAX) {
      log_err("Base image is too large.");
      goto cleanup;
    }
    primarySlot = (size_t)primaryInfo.imageSize;
  }
  if (!image_align_up(primarySlot, ARM64_BRANCH_ALIGNMENT, &primarySlot) ||
      primarySlot >= (size_t)ARM64_BRANCH_MAX_OFFSET ||
      primaryInfo.headerOffset > SIZE_MAX - primarySlot ||
      primaryInfo.headerOffset + primarySlot > SIZE_MAX - shim.fileSize) {
    log_err("Base image cannot reach the attached Shim with BL.");
    goto cleanup;
  }

  output.fileSize   = primaryInfo.headerOffset + primarySlot + shim.fileSize;
  output.fileBuffer = calloc(1, output.fileSize);
  if (output.fileBuffer == NULL)
    goto cleanup;
  memcpy(output.fileBuffer, primary.fileBuffer, primary.fileSize);
  memcpy(
      output.fileBuffer + primaryInfo.headerOffset + primarySlot,
      shim.fileBuffer, shim.fileSize);

  size_t manifestEntryCount = config->imageCount + 1;
  size_t manifestSize       = sizeof(ManifestHeader);
  for (size_t index = 0; index < config->imageCount; index++) {
    size_t entrySize = 0;
    if (!manifest_entry_size(config->images[index].name, &entrySize) ||
        manifestSize > SIZE_MAX - entrySize)
      goto cleanup;
    manifestSize += entrySize;
  }
  size_t shimEntrySize = 0;
  if (!manifest_entry_size("Attached Shim", &shimEntrySize) ||
      manifestSize > SIZE_MAX - shimEntrySize)
    goto cleanup;
  manifestSize += shimEntrySize;
  manifest = calloc(1, manifestSize);
  if (manifest == NULL)
    goto cleanup;
  manifest->header.magic         = MANIFEST_MAGIC;
  manifest->header.version.major = MANIFEST_VERSION_MAJOR;
  manifest->header.version.minor = MANIFEST_VERSION_MINOR;
  manifest->header.entryCount    = (uint32_t)manifestEntryCount;
  manifest->header.defaultEntry  = config->defaultEntry;
  manifest->header.timeoutMs     = config->timeoutMs;
  const uint8_t *originalHeader =
      primary.fileBuffer + primaryInfo.headerOffset;
  manifest->header.baseImage.instructions = image_read_le64(originalHeader);
  manifest->header.baseImage.imageSize =
      image_read_le64(originalHeader + 0x10);
  manifest->header.baseImage.manifestOffset =
      image_read_le64(originalHeader + 0x20);
  manifest->header.baseImage.manifestSize =
      image_read_le64(originalHeader + 0x28);
  manifest->header.baseImage.shimOffset =
      image_read_le64(originalHeader + 0x30);

  uint8_t *kernelHeader = output.fileBuffer + primaryInfo.headerOffset;
  if (!patch_kernel_entry(kernelHeader, primarySlot))
    goto cleanup;

  uint8_t       *manifestCursor = manifest->entries;
  ManifestEntry *baseEntry =
      write_manifest_entry(&manifestCursor, baseImage->name);
  if (baseEntry == NULL)
    goto cleanup;
  baseEntry->type   = MF_ENTRY_TYPE_LINUX;
  baseEntry->offset = 0;
  baseEntry->size   = primaryInfo.payloadSize;
  baseEntry->flags = MF_ENTRY_FLAG_BASE_IMAGE | MF_ENTRY_FLAG_EXECUTABLE;
  if (baseImage->fieldsPresent & PACK_IMAGE_FIELD_DEVICE_TREE_BLOB) {
    baseEntry->flags |= MF_ENTRY_FLAG_HAS_DEVICE_TREE_BLOB;
    baseEntry->deviceTreeBlobEntry = baseImage->deviceTreeBlobEntry;
  }
  baseEntry->alignment = baseImage->alignment;

  for (size_t index = 0; index < config->imageCount; index++) {
    if (index == config->baseImageIndex)
      continue;

    const PackImageConfig *image = &config->images[index];
    FileContent            extra = {.filePath = image->path};
    if (!load_file(&extra))
      goto cleanup;
    if (!inspect_typed_blob(&extra, image)) {
      free(extra.fileBuffer);
      goto cleanup;
    }
    if ((image->fieldsPresent & PACK_IMAGE_FIELD_COPY_ADDRESS) &&
        extra.fileSize > image->copySizeMax) {
      log_err(
          "[%s] image size 0x%zx exceeds CopySizeMax "
          "0x%llx.",
          image->section, extra.fileSize,
          (unsigned long long)image->copySizeMax);
      free(extra.fileBuffer);
      goto cleanup;
    }
    if ((image->fieldsPresent & PACK_IMAGE_FIELD_COPY_ADDRESS) &&
        image->copyAddress > UINT64_MAX - extra.fileSize) {
      log_err("Copy address range overflows: %s", image->section);
      free(extra.fileBuffer);
      goto cleanup;
    }

    size_t         runtimeSize   = output.fileSize - primaryInfo.headerOffset;
    size_t         alignment     = ((size_t)1) << image->alignment;
    size_t         imageOffset   = 0;
    size_t         payloadOffset = 0;
    size_t         payloadSize   = extra.fileSize;
    size_t         imageSlot     = extra.fileSize;
    Arm64ImageInfo extraInfo     = {0};

    if (image->type == MF_ENTRY_TYPE_LINUX) {
      if (!image_inspect_arm64_linux(&extra, &extraInfo)) {
        log_err("[%s] is not an ARM64 Linux Image.", image->section);
        free(extra.fileBuffer);
        goto cleanup;
      }
      payloadOffset = extraInfo.headerOffset;
      payloadSize   = extraInfo.payloadSize;
      imageSlot     = extraInfo.payloadSize;
      if (extraInfo.imageSize > imageSlot) {
        if (extraInfo.imageSize > SIZE_MAX) {
          free(extra.fileBuffer);
          goto cleanup;
        }
        imageSlot = (size_t)extraInfo.imageSize;
      }
      if (!align_linux_offset(
              runtimeSize, primaryInfo.textOffset, extraInfo.textOffset,
              alignment, &imageOffset) ||
          !image_align_up(imageSlot, 0x1000, &imageSlot)) {
        log_err("[%s] cannot satisfy Linux Image alignment.", image->section);
        free(extra.fileBuffer);
        goto cleanup;
      }
    }
    else if (!image_align_up(runtimeSize, alignment, &imageOffset)) {
      log_err("[%s] image alignment overflows.", image->section);
      free(extra.fileBuffer);
      goto cleanup;
    }

    if (manifest_entry_type_is_executable(image->type) &&
        (image->entryOffset > UINT32_MAX ||
         image->entryOffset > payloadSize ||
         payloadSize - (size_t)image->entryOffset < 4 ||
         (image->entryOffset & (ARM64_BRANCH_ALIGNMENT - 1)) != 0)) {
      log_err(
          "[%s] EntryOffset is outside the executable image.", image->section);
      free(extra.fileBuffer);
      goto cleanup;
    }
    if (imageOffset > SIZE_MAX - imageSlot ||
        primaryInfo.headerOffset > SIZE_MAX - imageOffset - imageSlot ||
        !resize_output(
            &output, primaryInfo.headerOffset + imageOffset + imageSlot)) {
      free(extra.fileBuffer);
      goto cleanup;
    }
    memcpy(
        output.fileBuffer + primaryInfo.headerOffset + imageOffset,
        extra.fileBuffer + payloadOffset, payloadSize);

    ManifestEntry *entry = write_manifest_entry(&manifestCursor, image->name);
    if (entry == NULL) {
      free(extra.fileBuffer);
      goto cleanup;
    }
    entry->type             = image->type;
    entry->entryOffset      = (uint32_t)image->entryOffset;
    entry->alignment        = image->alignment;
    entry->copySizeMax =
      (image->fieldsPresent & PACK_IMAGE_FIELD_COPY_SIZE_MAX)
        ? image->copySizeMax
        : 0;
    entry->offset = imageOffset;
    entry->size   = payloadSize;
    if (image->type == MF_ENTRY_TYPE_LINUX && payloadOffset != 0)
      entry->flags |= MF_ENTRY_FLAG_HAS_QC_ABOOT_HEADER;
    if (manifest_entry_type_is_executable(image->type))
      entry->flags |= MF_ENTRY_FLAG_EXECUTABLE;
    if (image->fieldsPresent & PACK_IMAGE_FIELD_COPY_ADDRESS) {
      entry->flags |= MF_ENTRY_FLAG_COPY;
      entry->loadAddress = image->copyAddress;
    }
    if (image->fieldsPresent & PACK_IMAGE_FIELD_DEVICE_TREE_BLOB) {
      entry->flags |= MF_ENTRY_FLAG_HAS_DEVICE_TREE_BLOB;
      entry->deviceTreeBlobEntry = image->deviceTreeBlobEntry;
    }
    free(extra.fileBuffer);
  }

  ManifestEntry *shimEntry =
      write_manifest_entry(&manifestCursor, "Attached Shim");
  if (shimEntry == NULL || manifestCursor != (uint8_t *)manifest + manifestSize)
    goto cleanup;
  shimEntry->offset      = primarySlot;
  shimEntry->size        = shim.fileSize;
  shimEntry->type        = MF_ENTRY_TYPE_SHIM;
  shimEntry->flags       = MF_ENTRY_FLAG_EXECUTABLE;
  shimEntry->alignment   = 2;

  size_t manifestOffset = 0;
  if (writeManifest) {
    size_t oldRuntimeSize = output.fileSize - primaryInfo.headerOffset;
    if (!image_align_up(oldRuntimeSize, sizeof(uint64_t), &manifestOffset)) {
      log_err("Failed to align manifest.");
      goto cleanup;
    }
    size_t manifestFileOffset = primaryInfo.headerOffset + manifestOffset;
    if (!resize_output(&output, manifestFileOffset + manifestSize)) {
      log_err("Failed to append manifest.");
      goto cleanup;
    }
    memcpy(output.fileBuffer + manifestFileOffset, manifest, manifestSize);
  }

  size_t runtimeImageSize = output.fileSize - primaryInfo.headerOffset;
  kernelHeader            = output.fileBuffer + primaryInfo.headerOffset;
  image_write_le64(kernelHeader + 0x10, runtimeImageSize);
  image_write_le64(kernelHeader + 0x20, manifestOffset);
  image_write_le64(kernelHeader + 0x28, manifestSize);
  image_write_le64(kernelHeader + 0x30, primarySlot);

  if (primaryInfo.headerOffset != 0) {
    if (runtimeImageSize > UINT32_MAX) {
      log_err("UNCOMPRESSED_IMG payload exceeds 32-bit size.");
      goto cleanup;
    }
    image_write_le32(output.fileBuffer + 0x10, (uint32_t)runtimeImageSize);
  }
  if (write_file_content(&output) != 0) {
    log_err("Failed to write packed image.");
    goto cleanup;
  }

  log_ok("Shim image packed successfully.");
  printf("\tBase image slot:    0x%zx\n", primarySlot);
  printf(
      "\tShim blob:          offset 0x%zx, size 0x%zx\n", primarySlot,
      shim.fileSize);
  if (writeManifest)
    printf(
        "\tManifest v%u.%u:     offset 0x%zx, size 0x%zx\n",
        MANIFEST_VERSION_MAJOR, MANIFEST_VERSION_MINOR, manifestOffset,
        manifestSize);
  else
    printf("\tManifest:           omitted\n");
  printf("\tOutput size:        0x%zx\n", runtimeImageSize);
  status = 0;

cleanup:
  free(primary.fileBuffer);
  free(shim.fileBuffer);
  free(output.fileBuffer);
  free(manifest);
  return status;
}
