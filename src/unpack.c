#include "shim_format.h"

#include <stdbool.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

typedef struct {
    uint8_t *buffer;
    size_t size;
    size_t headerOffset;
    const ShimManifest *manifest;
    const ShimManifestRecoveryHeader *recovery;
    const ShimManifestRecoveryEntry *recoveryEntries;
} PackedImage;

static bool join_path(char *result, size_t size, const char *directory,
                      const char *name) {
    int written = snprintf(result, size, "%s/%s", directory, name);
    return written >= 0 && (size_t)written < size;
}

static bool make_directory(const char *path) {
    if (mkdir(path, 0777) == 0)
        return true;
    return errno == EEXIST;
}

static bool write_buffer(const char *path, const uint8_t *buffer, size_t size) {
    FileContent file = {.filePath = path, .fileBuffer = (uint8_t *)buffer,
                         .fileSize = size};
    return write_file_content(&file) == 0;
}

static bool range_inside(size_t offset, size_t size, size_t limit) {
    return offset <= limit && size <= limit - offset;
}

static bool ranges_overlap(size_t firstOffset, size_t firstSize,
                           size_t secondOffset, size_t secondSize) {
    return firstOffset < secondOffset + secondSize &&
           secondOffset < firstOffset + firstSize;
}

static bool valid_name(const char name[32]) {
    const char *end = memchr(name, '\0', 32);
    if (end == NULL || end == name)
        return false;
    for (const char *current = name; current < end; current++) {
        unsigned char character = (unsigned char)*current;
        if (character < 0x20 || character == 0x7f)
            return false;
    }
    return true;
}

static bool load_packed(const char *path, PackedImage *packed) {
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
    if (!shim_inspect_arm64_image(&view, &image)) {
        printf("Error: Input is not an ARM64 Linux Image.\n");
        return false;
    }
    packed->headerOffset = image.headerOffset;
    if (packed->size - packed->headerOffset < SHIM_LINUX_HEADER_SIZE) {
        printf("Error: Input image is truncated.\n");
        return false;
    }

    const uint8_t *header = packed->buffer + packed->headerOffset;
    uint64_t manifestOffset = shim_read_le64(header + 0x20);
    uint64_t manifestSize = shim_read_le64(header + 0x28);
    uint64_t runtimeSize = shim_read_le64(header + 0x10);
    if (manifestOffset > SIZE_MAX || manifestSize > SIZE_MAX ||
        runtimeSize != packed->size - packed->headerOffset ||
        !range_inside((size_t)manifestOffset, (size_t)manifestSize,
                      (size_t)runtimeSize) ||
        manifestSize < sizeof(ShimManifestHeader)) {
        printf("Error: Input image has invalid Manifest bounds.\n");
        return false;
    }

    const uint8_t *manifestBuffer = header + (size_t)manifestOffset;
    if (memcmp(manifestBuffer, SHIM_MANIFEST_MAGIC, 8) != 0 ||
        shim_read_le32(manifestBuffer + 8) != SHIM_MANIFEST_VERSION) {
        printf("Error: Input image is not a supported reversible image.\n");
        return false;
    }
    const ShimManifest *manifest = (const ShimManifest *)manifestBuffer;
    if (manifest->header.headerSize != sizeof(ShimManifestHeader) ||
        manifest->header.entrySize != sizeof(ShimManifestEntry) ||
        manifest->header.entryCount == 0 ||
        manifest->header.entryCount > SHIM_MAX_ENTRIES ||
        manifest->header.imageSize != runtimeSize ||
        manifestSize > SHIM_MANIFEST_MAX_SIZE ||
        manifest->header.defaultEntry >= manifest->header.entryCount ||
        manifest->header.reserved[0] > SIZE_MAX ||
        manifest->header.reserved[1] > SIZE_MAX) {
        printf("Error: Input image has an invalid Manifest.\n");
        return false;
    }

    size_t recoveryOffset = (size_t)manifest->header.reserved[0];
    size_t recoverySize = (size_t)manifest->header.reserved[1];
    size_t entryCount = manifest->header.entryCount;
    size_t expectedRecoverySize = sizeof(ShimManifestRecoveryHeader) +
        entryCount * sizeof(ShimManifestRecoveryEntry);
    if (recoveryOffset == 0 || recoverySize != expectedRecoverySize ||
        !range_inside(recoveryOffset, recoverySize, (size_t)manifestSize) ||
        recoveryOffset != sizeof(ShimManifestHeader) +
            entryCount * sizeof(ShimManifestEntry)) {
        printf("Error: Input image has no supported Recovery data.\n");
        return false;
    }

    const uint8_t *recoveryBuffer = manifestBuffer + recoveryOffset;
    const ShimManifestRecoveryHeader *recovery =
        (const ShimManifestRecoveryHeader *)recoveryBuffer;
    if (memcmp(recovery->magic, SHIM_MANIFEST_RECOVERY_MAGIC, 8) != 0 ||
        recovery->version != SHIM_MANIFEST_RECOVERY_VERSION ||
        recovery->headerSize != sizeof(ShimManifestRecoveryHeader) ||
        recovery->entrySize != sizeof(ShimManifestRecoveryEntry) ||
        recovery->entryCount != entryCount || recovery->shimSize == 0 ||
        recovery->shimSize > SIZE_MAX) {
        printf("Error: Input image has an invalid Recovery header.\n");
        return false;
    }

    size_t primarySlot = (size_t)shim_read_le64(header + 0x30);
    if (primarySlot == 0 || recovery->shimSize > SIZE_MAX - primarySlot ||
        !range_inside(primarySlot, (size_t)recovery->shimSize,
                      (size_t)manifestOffset)) {
        printf("Error: Input image has invalid Shim bounds.\n");
        return false;
    }
    for (size_t index = 0; index < entryCount; index++) {
        const ShimManifestEntry *entry = &manifest->entries[index];
        if (shim_type_name(entry->type) == NULL || !valid_name(entry->name) ||
            entry->offset > SIZE_MAX || entry->size > SIZE_MAX ||
            !range_inside((size_t)entry->offset, (size_t)entry->size,
                          (size_t)manifestOffset)) {
            printf("Error: Input image has an invalid Manifest entry.\n");
            return false;
        }
    }

    packed->manifest = manifest;
    packed->recovery = recovery;
    packed->recoveryEntries =
        (const ShimManifestRecoveryEntry *)(recoveryBuffer +
            sizeof(ShimManifestRecoveryHeader));

    if (manifest->entries[0].offset != 0 ||
        manifest->entries[0].type != SHIM_ENTRY_TYPE_LINUX ||
        manifest->entries[0].flags != SHIM_ENTRY_FLAG_BASE_IMAGE ||
        manifest->entries[0].size < SHIM_LINUX_HEADER_SIZE)
        return false;
    size_t shimEnd = primarySlot + (size_t)recovery->shimSize;
    for (size_t index = 0; index < entryCount; index++) {
        const ShimManifestEntry *entry = &manifest->entries[index];
        const ShimManifestRecoveryEntry *metadata =
            &packed->recoveryEntries[index];
        uint32_t allowedFlags = index == 0
            ? SHIM_ENTRY_FLAG_BASE_IMAGE
            : SHIM_ENTRY_FLAG_COPY;
        if ((entry->flags & ~allowedFlags) != 0 ||
            metadata->alignment < SHIM_BRANCH_ALIGNMENT ||
            metadata->alignment > UINT32_MAX ||
            (metadata->alignment & (metadata->alignment - 1)) != 0 ||
            metadata->sourcePrefixSize > SHIM_WRAPPER_SIZE ||
            (metadata->sourcePrefixSize != 0 &&
             metadata->sourcePrefixSize != SHIM_WRAPPER_SIZE) ||
            (entry->flags & SHIM_ENTRY_FLAG_COPY) != 0 &&
                (entry->loadAddress == 0 || metadata->copySizeMax == 0 ||
                 entry->size > metadata->copySizeMax)) {
            printf("Error: Input image has invalid recovery metadata.\n");
            return false;
        }
        if (index == 0) {
            if (entry->size > primarySlot)
                return false;
            continue;
        }
        if (ranges_overlap((size_t)entry->offset, (size_t)entry->size,
                           primarySlot, (size_t)recovery->shimSize)) {
            printf("Error: Manifest entry overlaps the attached Shim.\n");
            return false;
        }
        for (size_t previous = 1; previous < index; previous++) {
            const ShimManifestEntry *other = &manifest->entries[previous];
            if (ranges_overlap((size_t)entry->offset, (size_t)entry->size,
                               (size_t)other->offset,
                               (size_t)other->size)) {
                printf("Error: Manifest entries overlap.\n");
                return false;
            }
        }
        if ((size_t)entry->offset < shimEnd)
            return false;
    }
    return true;
}

static bool restore_files(const PackedImage *packed, const char *directory) {
    char path[SHIM_PATH_SIZE * 2];
    const ShimManifest *manifest = packed->manifest;
    const ShimManifestRecoveryHeader *recovery = packed->recovery;
    size_t entryCount = manifest->header.entryCount;
    size_t primarySlot = (size_t)shim_read_le64(
        packed->buffer + packed->headerOffset + 0x30);

    FileContent base = {0};
    base.fileSize = (size_t)manifest->entries[0].size;
    base.fileBuffer = malloc(base.fileSize);
    if (base.fileBuffer == NULL)
        return false;
    memcpy(base.fileBuffer, packed->buffer + packed->headerOffset,
           base.fileSize);
    memcpy(base.fileBuffer, recovery->baseHeader,
           sizeof(recovery->baseHeader));
    {
        const ShimManifestRecoveryEntry *baseMetadata =
            &packed->recoveryEntries[0];
        size_t prefixSize = baseMetadata->sourcePrefixSize;
        if (prefixSize > SHIM_WRAPPER_SIZE ||
            !join_path(path, sizeof(path), directory, "BaseImage") ||
            prefixSize + base.fileSize > SIZE_MAX) {
            free(base.fileBuffer);
            return false;
        }
        size_t restoredSize = prefixSize + base.fileSize;
        uint8_t *restored = malloc(restoredSize);
        if (restored == NULL) {
            free(base.fileBuffer);
            return false;
        }
        memcpy(restored, baseMetadata->sourcePrefix, prefixSize);
        memcpy(restored + prefixSize, base.fileBuffer, base.fileSize);
        bool result = write_buffer(path, restored, restoredSize);
        free(restored);
        free(base.fileBuffer);
        if (!result)
            return false;
    }

    if (!join_path(path, sizeof(path), directory, "Shim.bin") ||
        !write_buffer(path, packed->buffer + packed->headerOffset + primarySlot,
                      (size_t)recovery->shimSize))
        return false;

    for (size_t index = 1; index < entryCount; index++) {
        const ShimManifestEntry *entry = &manifest->entries[index];
        const ShimManifestRecoveryEntry *metadata =
            &packed->recoveryEntries[index];
        const char *typeName = shim_type_name(entry->type);
        char name[64];
        snprintf(name, sizeof(name), "Image-%02zu-%s", index, typeName);
        if (!join_path(path, sizeof(path), directory, name))
            return false;
        size_t prefixSize = metadata->sourcePrefixSize;
        if (prefixSize > SHIM_WRAPPER_SIZE ||
            prefixSize + (size_t)entry->size < prefixSize)
            return false;
        size_t outputSize = prefixSize + (size_t)entry->size;
        uint8_t *output = malloc(outputSize);
        if (output == NULL)
            return false;
        memcpy(output, metadata->sourcePrefix, prefixSize);
        memcpy(output + prefixSize,
               packed->buffer + packed->headerOffset + entry->offset,
               (size_t)entry->size);
        bool result = write_buffer(path, output, outputSize);
        free(output);
        if (!result)
            return false;
    }
    return true;
}

static bool write_config(const PackedImage *packed, const char *directory) {
    char path[SHIM_PATH_SIZE * 2];
    if (!join_path(path, sizeof(path), directory, "config.cfg"))
        return false;
    FILE *file = fopen(path, "w");
    if (file == NULL)
        return false;

    const ShimManifest *manifest = packed->manifest;
        fprintf(file, "[Pack]\nShim=Shim.bin\nOutput=RepackedKernel\n");
        fprintf(file, "Default=Image-%02u\nTimeout=%u\n\n",
            manifest->header.defaultEntry, manifest->header.timeoutMs);
    for (size_t index = 0; index < manifest->header.entryCount; index++) {
        const ShimManifestEntry *entry = &manifest->entries[index];
        const ShimManifestRecoveryEntry *metadata =
            &packed->recoveryEntries[index];
        const char *typeName = shim_type_name(entry->type);
        fprintf(file, "[Image-%02zu]\nName=%s\n", index, entry->name);
        if (index == 0)
            fprintf(file, "Path=BaseImage\n");
        else {
            char name[64];
            snprintf(name, sizeof(name), "Image-%02zu-%s", index, typeName);
            fprintf(file, "Path=%s\n", name);
        }
        fprintf(file, "Type=%s\nBaseImage=%s\nAlign=0x%llx\n",
                typeName, index == 0 ? "true" : "false",
                (unsigned long long)metadata->alignment);
        if (entry->flags & SHIM_ENTRY_FLAG_COPY)
            fprintf(file, "CopyTo=0x%llx\nCopySizeMax=0x%llx\n",
                    (unsigned long long)entry->loadAddress,
                    (unsigned long long)metadata->copySizeMax);
        if (shim_is_executable_type(entry->type))
            fprintf(file, "EntryOffset=0x%llx\n",
                    (unsigned long long)entry->entryOffset);
        fprintf(file, "\n");
    }
    fprintf(file, "[Manifest]\nType=Manifest\n");
    return fclose(file) == 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: UnpackTool <PatchedKernel> <OutputDir>\n");
        return EINVAL;
    }
    PackedImage packed = {0};
    if (!load_packed(argv[1], &packed)) {
        free(packed.buffer);
        return EINVAL;
    }
    if (!make_directory(argv[2]) || !restore_files(&packed, argv[2]) ||
        !write_config(&packed, argv[2])) {
        printf("Error: Failed to write unpacked files.\n");
        free(packed.buffer);
        return EINVAL;
    }
    printf("Kernel image unpacked successfully.\n");
    free(packed.buffer);
    return 0;
}