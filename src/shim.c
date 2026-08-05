#include "shim.h"
#include "utils.h"

#include <stdbool.h>

typedef struct {
    size_t headerOffset;
    size_t payloadSize;
    uint64_t textOffset;
    uint64_t imageSize;
} Arm64ImageInfo;

static uint32_t read_le32(const uint8_t *buffer) {
    uint32_t value = 0;
    for (int index = 3; index >= 0; index--)
        value = (value << 8) | buffer[index];
    return value;
}

static uint64_t read_le64(const uint8_t *buffer) {
    uint64_t value = 0;
    for (int index = 7; index >= 0; index--)
        value = (value << 8) | buffer[index];
    return value;
}

static void write_le32(uint8_t *buffer, uint32_t value) {
    for (int index = 0; index < 4; index++)
        buffer[index] = value >> (index * 8) & 0xff;
}

static void write_le64(uint8_t *buffer, uint64_t value) {
    for (int index = 0; index < 8; index++)
        buffer[index] = value >> (index * 8) & 0xff;
}

static bool align_up_size(size_t value, size_t alignment, size_t *result) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
        value > SIZE_MAX - (alignment - 1))
        return false;
    *result = (value + alignment - 1) & ~(alignment - 1);
    return true;
}

static bool inspect_arm64_image(const FileContent *file, Arm64ImageInfo *info) {
    static const uint8_t arm64Magic[] = {'A', 'R', 'M', 0x64};

    info->headerOffset = 0;
    if (file->fileSize >= 0x14 &&
        memcmp(file->fileBuffer, "UNCOMPRESSED_IMG", 0x10) == 0)
        info->headerOffset = 0x14;

    if (file->fileSize < info->headerOffset + 0x40 ||
        memcmp(file->fileBuffer + info->headerOffset + 0x38,
               arm64Magic, sizeof(arm64Magic)) != 0)
        return false;

    info->payloadSize = file->fileSize - info->headerOffset;
    info->textOffset = read_le64(file->fileBuffer + info->headerOffset + 0x08);
    info->imageSize = read_le64(file->fileBuffer + info->headerOffset + 0x10);
    return true;
}

static bool align_linux_offset(size_t minimum,
                               uint64_t primaryTextOffset,
                               uint64_t imageTextOffset,
                               size_t alignment,
                               size_t *result) {
    const size_t mask = SHIM_LINUX_ALIGNMENT - 1;
    size_t requiredResidue =
        ((size_t)imageTextOffset - (size_t)primaryTextOffset) & mask;
    size_t adjustment = (requiredResidue - (minimum & mask)) & mask;
    if (minimum > SIZE_MAX - adjustment)
        return false;
    *result = minimum + adjustment;
    return (*result & (alignment - 1)) == 0;
}

static bool load_file(FileContent *file) {
    if (!get_file_size(file))
        return false;
    file->fileBuffer = malloc(file->fileSize);
    return file->fileBuffer != NULL && read_file_content(file) != NULL;
}

static bool resize_output(FileContent *output, size_t newSize) {
    uint8_t *resized = realloc(output->fileBuffer, newSize);
    if (resized == NULL)
        return false;
    if (newSize > output->fileSize)
        memset(resized + output->fileSize, 0, newSize - output->fileSize);
    output->fileBuffer = resized;
    output->fileSize = newSize;
    return true;
}

static bool is_executable_type(uint32_t type) {
    return type == SHIM_ENTRY_TYPE_LINUX ||
           type == SHIM_ENTRY_TYPE_FREE_EXEC ||
           type == SHIM_ENTRY_TYPE_SHIM;
}

static bool inspect_typed_blob(const FileContent *file,
                               const ShimImageConfig *image) {
    if (image->type == SHIM_ENTRY_TYPE_DTB) {
        static const uint8_t fdtMagic[] = {0xd0, 0x0d, 0xfe, 0xed};
        if (file->fileSize < sizeof(fdtMagic) ||
            memcmp(file->fileBuffer, fdtMagic, sizeof(fdtMagic)) != 0) {
            printf("Error: [%s] is not a flattened device tree blob.\n",
                   image->section);
            return false;
        }
    } else if (image->type == SHIM_ENTRY_TYPE_MANIFEST) {
        if (file->fileSize < sizeof(ShimManifestHeader) ||
            memcmp(file->fileBuffer, SHIM_MANIFEST_MAGIC, 8) != 0 ||
            read_le32(file->fileBuffer + 8) != SHIM_MANIFEST_VERSION) {
            printf("Error: [%s] is not a v%u Shim manifest.\n",
                   image->section, SHIM_MANIFEST_VERSION);
            return false;
        }
    }
    return true;
}

static bool patch_kernel_entry(uint8_t *kernelHeader, size_t shimOffset) {
    if ((shimOffset & (SHIM_BRANCH_ALIGNMENT - 1)) != 0 ||
        shimOffset >= (size_t)SHIM_BRANCH_MAX_OFFSET) {
        printf("Error: Shim is outside the ARM64 BL range.\n");
        return false;
    }

    uint32_t firstInstruction = read_le32(kernelHeader);
    uint32_t secondInstruction = read_le32(kernelHeader + 4);
    bool firstIsBranch = (firstInstruction & 0xfc000000U) == 0x14000000U;
    bool secondIsBranch = (secondInstruction & 0xfc000000U) == 0x14000000U;

    if (firstIsBranch && !secondIsBranch) {
        uint32_t movedBranch = 0x14000000U |
            ((firstInstruction - 1U) & 0x03ffffffU);
        write_le32(kernelHeader + 4, movedBranch);
    } else if (!secondIsBranch) {
        printf("Error: Base image has no supported entry branch.\n");
        return false;
    }

    uint32_t branchImmediate = (uint32_t)(shimOffset >> 2);
    write_le32(kernelHeader, 0x94000000U | branchImmediate);
    return true;
}

int PackShim(const ShimPackConfig *config) {
    const ShimImageConfig *baseImage =
        &config->images[config->baseImageIndex];
    bool writeManifest = config->manifest.present;
    int status = -EINVAL;

    FileContent primary = {.filePath = baseImage->path};
    FileContent shim = {.filePath = config->shim};
    FileContent output = {.filePath = config->output};

    if (!load_file(&primary) || !load_file(&shim))
        goto cleanup;
    if (shim.fileSize == 0) {
        printf("Error: Shim must be a non-empty blob.\n");
        goto cleanup;
    }

    Arm64ImageInfo primaryInfo = {0};
    if (!inspect_arm64_image(&primary, &primaryInfo)) {
        printf("Error: Base image is not an ARM64 Linux Image.\n");
        goto cleanup;
    }

    size_t primarySlot = primaryInfo.payloadSize;
    if (primaryInfo.imageSize > primarySlot) {
        if (primaryInfo.imageSize > SIZE_MAX) {
            printf("Error: Base image is too large.\n");
            goto cleanup;
        }
        primarySlot = (size_t)primaryInfo.imageSize;
    }
    if (!align_up_size(primarySlot, SHIM_BRANCH_ALIGNMENT, &primarySlot) ||
        primarySlot >= (size_t)SHIM_BRANCH_MAX_OFFSET ||
        primaryInfo.headerOffset > SIZE_MAX - primarySlot ||
        primaryInfo.headerOffset + primarySlot > SIZE_MAX - shim.fileSize) {
        printf("Error: Base image cannot reach the attached Shim with BL.\n");
        goto cleanup;
    }

    output.fileSize = primaryInfo.headerOffset + primarySlot + shim.fileSize;
    output.fileBuffer = calloc(1, output.fileSize);
    if (output.fileBuffer == NULL)
        goto cleanup;
    memcpy(output.fileBuffer, primary.fileBuffer, primary.fileSize);
    memcpy(output.fileBuffer + primaryInfo.headerOffset + primarySlot,
           shim.fileBuffer, shim.fileSize);

    uint8_t *kernelHeader = output.fileBuffer + primaryInfo.headerOffset;
    if (!patch_kernel_entry(kernelHeader, primarySlot))
        goto cleanup;

    ShimManifest manifest = {0};
    memcpy(manifest.header.magic, SHIM_MANIFEST_MAGIC, 8);
    manifest.header.version = SHIM_MANIFEST_VERSION;
    manifest.header.headerSize = sizeof(ShimManifestHeader);
    manifest.header.entrySize = sizeof(ShimManifestEntry);
    manifest.header.entryCount = (uint32_t)config->imageCount;
    manifest.header.defaultEntry = config->defaultEntry;
    manifest.header.timeoutMs = config->timeoutMs;

    ShimManifestEntry *baseEntry = &manifest.entries[0];
    memcpy(baseEntry->name, baseImage->name, strlen(baseImage->name) + 1);
    baseEntry->type = SHIM_ENTRY_TYPE_LINUX;
    baseEntry->entryOffset = baseImage->entryOffset;
    baseEntry->offset = 0;
    baseEntry->size = primaryInfo.payloadSize;
    baseEntry->flags = SHIM_ENTRY_FLAG_BASE_IMAGE;

    size_t manifestIndex = 1;
    for (size_t index = 0; index < config->imageCount; index++) {
        if (index == config->baseImageIndex)
            continue;

        const ShimImageConfig *image = &config->images[index];
        FileContent extra = {.filePath = image->path};
        if (!load_file(&extra))
            goto cleanup;
        if (!inspect_typed_blob(&extra, image)) {
            free(extra.fileBuffer);
            goto cleanup;
        }
        if (image->hasCopyAddress && extra.fileSize > image->copySizeMax) {
            printf("Error: [%s] image size 0x%zx exceeds CopySizeMax "
                   "0x%llx.\n", image->section, extra.fileSize,
                   (unsigned long long)image->copySizeMax);
            free(extra.fileBuffer);
            goto cleanup;
        }
        if (image->hasCopyAddress &&
            image->copyAddress > UINT64_MAX - extra.fileSize) {
            printf("Error: Copy address range overflows: %s\n",
                   image->section);
            free(extra.fileBuffer);
            goto cleanup;
        }

        size_t runtimeSize = output.fileSize - primaryInfo.headerOffset;
        size_t imageOffset = 0;
        size_t payloadOffset = 0;
        size_t payloadSize = extra.fileSize;
        size_t imageSlot = extra.fileSize;
        Arm64ImageInfo extraInfo = {0};

        if (image->type == SHIM_ENTRY_TYPE_LINUX) {
            if (!inspect_arm64_image(&extra, &extraInfo)) {
                printf("Error: [%s] is not an ARM64 Linux Image.\n",
                       image->section);
                free(extra.fileBuffer);
                goto cleanup;
            }
            payloadOffset = extraInfo.headerOffset;
            payloadSize = extraInfo.payloadSize;
            imageSlot = extraInfo.payloadSize;
            if (extraInfo.imageSize > imageSlot) {
                if (extraInfo.imageSize > SIZE_MAX) {
                    free(extra.fileBuffer);
                    goto cleanup;
                }
                imageSlot = (size_t)extraInfo.imageSize;
            }
            if (!align_linux_offset(runtimeSize, primaryInfo.textOffset,
                                    extraInfo.textOffset,
                                    (size_t)image->alignment, &imageOffset) ||
                !align_up_size(imageSlot, 0x1000, &imageSlot)) {
                printf("Error: [%s] cannot satisfy Linux Image alignment.\n",
                       image->section);
                free(extra.fileBuffer);
                goto cleanup;
            }
        } else if (!align_up_size(runtimeSize, (size_t)image->alignment,
                                  &imageOffset)) {
            printf("Error: [%s] image alignment overflows.\n", image->section);
            free(extra.fileBuffer);
            goto cleanup;
        }

        if (is_executable_type(image->type) &&
            (image->entryOffset > payloadSize ||
             payloadSize - (size_t)image->entryOffset < 4 ||
             (image->entryOffset & (SHIM_BRANCH_ALIGNMENT - 1)) != 0)) {
            printf("Error: [%s] EntryOffset is outside the executable image.\n",
                   image->section);
            free(extra.fileBuffer);
            goto cleanup;
        }
        if (imageOffset > SIZE_MAX - imageSlot ||
            primaryInfo.headerOffset > SIZE_MAX - imageOffset - imageSlot ||
            !resize_output(&output,
                           primaryInfo.headerOffset + imageOffset + imageSlot)) {
            free(extra.fileBuffer);
            goto cleanup;
        }
        memcpy(output.fileBuffer + primaryInfo.headerOffset + imageOffset,
               extra.fileBuffer + payloadOffset, payloadSize);

        ShimManifestEntry *entry = &manifest.entries[manifestIndex++];
        memcpy(entry->name, image->name, strlen(image->name) + 1);
        entry->type = image->type;
        entry->entryOffset = image->entryOffset;
        entry->offset = imageOffset;
        entry->size = payloadSize;
        if (image->hasCopyAddress) {
            entry->flags |= SHIM_ENTRY_FLAG_COPY;
            entry->loadAddress = image->copyAddress;
        }
        free(extra.fileBuffer);
    }

    size_t manifestOffset = 0;
    size_t manifestSize = 0;
    if (writeManifest) {
        manifestSize = sizeof(ShimManifestHeader) +
            manifest.header.entryCount * sizeof(ShimManifestEntry);
        size_t oldRuntimeSize = output.fileSize - primaryInfo.headerOffset;
        if (!align_up_size(oldRuntimeSize, sizeof(uint64_t), &manifestOffset)) {
            printf("Error: Failed to align Shim manifest.\n");
            goto cleanup;
        }
        size_t manifestFileOffset = primaryInfo.headerOffset + manifestOffset;
        if (manifestSize > SHIM_MANIFEST_MAX_SIZE ||
            manifestSize > SIZE_MAX - manifestFileOffset ||
            !resize_output(&output, manifestFileOffset + manifestSize)) {
            printf("Error: Failed to append Shim manifest.\n");
            goto cleanup;
        }
        manifest.header.imageSize = manifestOffset + manifestSize;
        memcpy(output.fileBuffer + manifestFileOffset, &manifest, manifestSize);
    }

    size_t runtimeImageSize = output.fileSize - primaryInfo.headerOffset;
    kernelHeader = output.fileBuffer + primaryInfo.headerOffset;
    write_le64(kernelHeader + 0x10, runtimeImageSize);
    write_le64(kernelHeader + 0x20, manifestOffset);
    write_le64(kernelHeader + 0x28, manifestSize);
    write_le64(kernelHeader + 0x30, primarySlot);

    if (primaryInfo.headerOffset != 0) {
        if (runtimeImageSize > UINT32_MAX) {
            printf("Error: UNCOMPRESSED_IMG payload exceeds 32-bit size.\n");
            goto cleanup;
        }
        write_le32(output.fileBuffer + 0x10, (uint32_t)runtimeImageSize);
    }
    if (write_file_content(&output) != 0) {
        printf("Error: Failed to write packed image.\n");
        goto cleanup;
    }

    printf("Shim image packed successfully.\n");
    printf("  Base image slot:    0x%zx\n", primarySlot);
    printf("  Shim blob:          offset 0x%zx, size 0x%zx\n",
           primarySlot, shim.fileSize);
    if (writeManifest)
        printf("  Manifest v%u:       offset 0x%zx, size 0x%zx\n",
               SHIM_MANIFEST_VERSION, manifestOffset, manifestSize);
    else
        printf("  Manifest:           omitted\n");
    printf("  Output size:        0x%zx\n", runtimeImageSize);
    status = 0;

cleanup:
    free(primary.fileBuffer);
    free(shim.fileBuffer);
    free(output.fileBuffer);
    return status;
}
