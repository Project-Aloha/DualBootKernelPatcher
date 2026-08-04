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
    if (alignment == 0 || value > SIZE_MAX - (alignment - 1))
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

static bool align_image_offset(size_t minimum,
                               uint64_t primaryTextOffset,
                               uint64_t imageTextOffset,
                               size_t alignment,
                               size_t *result) {
    const size_t mask = SHIM_KERNEL_ALIGNMENT - 1;
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

static bool initialize_output(const FileContent *primary,
                              const Arm64ImageInfo *primaryInfo,
                              size_t primarySlot,
                              const FileContent *shim,
                              const FileContent *loader,
                              FileContent *output) {
    if (primaryInfo->headerOffset > SIZE_MAX - primarySlot ||
        primaryInfo->headerOffset + primarySlot > SIZE_MAX - shim->fileSize ||
        loader->fileSize > primarySlot)
        return false;

    output->fileSize = primaryInfo->headerOffset + primarySlot + shim->fileSize;
    output->fileBuffer = calloc(1, output->fileSize);
    if (output->fileBuffer == NULL)
        return false;

    memcpy(output->fileBuffer, primary->fileBuffer, primary->fileSize);
    memcpy(output->fileBuffer + primaryInfo->headerOffset + primarySlot,
           shim->fileBuffer, shim->fileSize);

    uint8_t *kernelHeader = output->fileBuffer + primaryInfo->headerOffset;
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

    write_le32(kernelHeader, 0x14000010U);
    memcpy(kernelHeader + 0x40, loader->fileBuffer + 0x40,
           loader->fileSize - 0x40);
    write_le64(kernelHeader + 0x30, primarySlot);
    return true;
}

int PackShim(const ShimPackConfig *config) {
    size_t extraImageCount = config->imageCount - 1;
    const ShimImageConfig *baseImage =
        &config->images[config->baseImageIndex];
    int status = -EINVAL;

    FileContent primary = {0};
    FileContent shim = {0};
    FileContent loader = {0};
    FileContent output = {0};

    primary.filePath = baseImage->path;
    shim.filePath = config->shim;
    output.filePath = config->output;
    loader.filePath = config->loader;

    if (!load_file(&primary) || !load_file(&shim) || !load_file(&loader))
        goto cleanup;

    Arm64ImageInfo primaryInfo = {0};
    Arm64ImageInfo shimInfo = {0};
    if (!inspect_arm64_image(&primary, &primaryInfo)) {
        printf("Error: Base image is not an ARM64 Linux Image.\n");
        goto cleanup;
    }
    if (!inspect_arm64_image(&shim, &shimInfo) || shimInfo.headerOffset != 0) {
        printf("Error: Shim is not a raw ARM64 Image.\n");
        goto cleanup;
    }
    if (shimInfo.imageSize == 0 || shim.fileSize != shimInfo.imageSize) {
        printf("Error: Shim file size must match its non-zero Image size.\n");
        goto cleanup;
    }
    bool isSimpleShim =
        memcmp(shim.fileBuffer + 8, SIMPLE_SHIM_MAGIC,
               sizeof(SIMPLE_SHIM_MAGIC)) == 0;
    if (isSimpleShim) {
        if (config->defaultEntry != 0 || config->timeoutMs != 0) {
            printf("Error: Simplified assembly Shim requires the base image "
                   "as Default and Timeout=0.\n");
            goto cleanup;
        }
        if (config->imageCount != 2) {
            printf("Error: Simplified assembly Shim requires exactly one "
                   "base image and one CopyTo image.\n");
            goto cleanup;
        }
        for (size_t index = 0; index < config->imageCount; index++) {
            if (index != config->baseImageIndex &&
                !config->images[index].hasCopyAddress) {
                printf("Error: Simplified assembly Shim requires its second "
                       "image to set CopyTo.\n");
                goto cleanup;
            }
        }
    }
    if (loader.fileSize <= 0x40 ||
        memcmp(loader.fileBuffer + 8, SHIM_LOADER_MAGIC,
               sizeof(SHIM_LOADER_MAGIC)) != 0) {
        printf("Error: Invalid ShimLoader image.\n");
        goto cleanup;
    }

    size_t primarySlot = primaryInfo.payloadSize;
    if (primaryInfo.imageSize > primarySlot)
        primarySlot = (size_t)primaryInfo.imageSize;
    if (!align_up_size(primarySlot, 0x1000, &primarySlot) ||
        primarySlot > SIZE_MAX - primaryInfo.headerOffset) {
        printf("Error: Base image is too large.\n");
        goto cleanup;
    }

    if (!initialize_output(&primary, &primaryInfo, primarySlot, &shim,
                           &loader, &output))
        goto cleanup;

    ShimManifest manifest = {0};
    memcpy(manifest.header.magic, SHIM_MANIFEST_MAGIC, 8);
    manifest.header.version = SHIM_MANIFEST_VERSION;
    manifest.header.headerSize = sizeof(ShimManifestHeader);
    manifest.header.entrySize = sizeof(ShimManifestEntry);
    manifest.header.entryCount = (uint32_t)extraImageCount + 1;
    manifest.header.defaultEntry = config->defaultEntry;
    manifest.header.timeoutMs = config->timeoutMs;

    memcpy(manifest.entries[0].name, baseImage->name, strlen(baseImage->name) + 1);
    manifest.entries[0].offset = 0;
    manifest.entries[0].size = primaryInfo.payloadSize;
    manifest.entries[0].type = SHIM_ENTRY_TYPE_LINUX;
    manifest.entries[0].flags = SHIM_ENTRY_FLAG_BASE_IMAGE;

    size_t manifestIndex = 1;
    for (size_t index = 0; index < config->imageCount; index++) {
        if (index == config->baseImageIndex)
            continue;

        const ShimImageConfig *image = &config->images[index];
        FileContent extra = {.filePath = image->path};
        if (!load_file(&extra))
            goto cleanup;
        if (image->hasCopyAddress && extra.fileSize > image->copySizeMax) {
            printf("Error: [%s] image size 0x%zx exceeds CopySizeMax "
                   "0x%llx.\n", image->section, extra.fileSize,
                   (unsigned long long)image->copySizeMax);
            free(extra.fileBuffer);
            goto cleanup;
        }

        size_t runtimeSize = output.fileSize - primaryInfo.headerOffset;
        size_t imageOffset = 0;
        size_t imageSlot = 0;
        size_t payloadOffset = 0;
        size_t payloadSize = extra.fileSize;
        Arm64ImageInfo extraInfo = {0};

        if (image->hasCopyAddress) {
            if (image->copyAddress > UINT64_MAX - extra.fileSize) {
                printf("Error: Copy address range overflows: %s\n",
                       image->section);
                free(extra.fileBuffer);
                goto cleanup;
            }
            if (!align_up_size(runtimeSize, (size_t)image->alignment,
                               &imageOffset)) {
                free(extra.fileBuffer);
                goto cleanup;
            }
            imageSlot = extra.fileSize;
        } else {
            if (!inspect_arm64_image(&extra, &extraInfo)) {
                printf("Error: Image without CopyTo is not an ARM64 Linux "
                       "Image: %s\n", extra.filePath);
                free(extra.fileBuffer);
                goto cleanup;
            }
            payloadOffset = extraInfo.headerOffset;
            payloadSize = extraInfo.payloadSize;
            imageSlot = extraInfo.payloadSize;
            if (extraInfo.imageSize > imageSlot)
                imageSlot = (size_t)extraInfo.imageSize;
            if (!align_image_offset(runtimeSize, primaryInfo.textOffset,
                             extraInfo.textOffset,
                             (size_t)image->alignment, &imageOffset) ||
                !align_up_size(imageSlot, 0x1000, &imageSlot)) {
                  printf("Error: [%s] cannot satisfy Align=0x%llx and ARM64 "
                      "Image text_offset constraints.\n", image->section,
                      (unsigned long long)image->alignment);
                free(extra.fileBuffer);
                goto cleanup;
            }
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
        entry->offset = imageOffset;
        entry->size = payloadSize;
        if (image->hasCopyAddress) {
            entry->type = SHIM_ENTRY_TYPE_EXECUTABLE;
            entry->flags = SHIM_ENTRY_FLAG_COPY;
            entry->loadAddress = image->copyAddress;
        } else {
            entry->type = SHIM_ENTRY_TYPE_LINUX;
        }
        free(extra.fileBuffer);
    }

    if (isSimpleShim) {
        ShimManifestEntry *payload = &manifest.entries[1];
        uint8_t *shimHeader = output.fileBuffer + primaryInfo.headerOffset +
                              primarySlot;
        write_le64(shimHeader + SIMPLE_SHIM_PAYLOAD_OFFSET, payload->offset);
        write_le64(shimHeader + SIMPLE_SHIM_COPY_ADDRESS_OFFSET,
               payload->loadAddress);
        write_le64(shimHeader + SIMPLE_SHIM_COPY_SIZE_OFFSET, payload->size);

        uint8_t *kernelHeader = output.fileBuffer + primaryInfo.headerOffset;
        write_le64(kernelHeader + 0x20, 0);
        write_le64(kernelHeader + 0x28, 0);

        if (primaryInfo.headerOffset != 0) {
            size_t imageSize = output.fileSize - primaryInfo.headerOffset;
            if (imageSize > UINT32_MAX) {
                printf("Error: UNCOMPRESSED_IMG payload exceeds 32-bit size.\n");
                goto cleanup;
            }
            write_le32(output.fileBuffer + 0x10, (uint32_t)imageSize);
        }
        if (write_file_content(&output) != 0) {
            printf("Error: Failed to write simplified Shim image.\n");
            goto cleanup;
        }

        printf("Simplified Shim image packed successfully.\n");
        printf("  Payload:            offset 0x%llx, CopyTo 0x%llx, "
               "size 0x%llx\n",
               (unsigned long long)payload->offset,
               (unsigned long long)payload->loadAddress,
               (unsigned long long)payload->size);
        printf("  Output size:        0x%zx\n", output.fileSize);
        status = 0;
        goto cleanup;
    }

    size_t manifestSize = sizeof(ShimManifestHeader) +
                          manifest.header.entryCount * sizeof(ShimManifestEntry);
    size_t manifestOffset = output.fileSize - primaryInfo.headerOffset;
    size_t oldOutputSize = output.fileSize;
    if (manifestSize > SHIM_MANIFEST_MAX_SIZE ||
        manifestSize > SIZE_MAX - oldOutputSize ||
        !resize_output(&output, oldOutputSize + manifestSize)) {
        printf("Error: Failed to append Shim manifest.\n");
        goto cleanup;
    }
    manifest.header.imageSize = manifestOffset + manifestSize;
    memcpy(output.fileBuffer + oldOutputSize, &manifest, manifestSize);

    uint8_t *kernelHeader = output.fileBuffer + primaryInfo.headerOffset;
    write_le64(kernelHeader + 0x20, manifestOffset);
    write_le64(kernelHeader + 0x28, manifestSize);

    if (primaryInfo.headerOffset != 0) {
        if (manifest.header.imageSize > UINT32_MAX) {
            printf("Error: UNCOMPRESSED_IMG payload exceeds 32-bit size.\n");
            goto cleanup;
        }
        write_le32(output.fileBuffer + 0x10, (uint32_t)manifest.header.imageSize);
    }

    if (write_file_content(&output) != 0) {
        printf("Error: Failed to write Shim image.\n");
        goto cleanup;
    }

    printf("Shim image packed successfully.\n");
    printf("  Base image slot:    0x%zx\n", primarySlot);
    printf("  Shim slot:          0x%zx\n", shim.fileSize);
        printf("  Manifest blob:      offset 0x%zx, size 0x%zx\n",
            manifestOffset, manifestSize);
    for (uint32_t index = 0; index < manifest.header.entryCount; index++)
        printf("  Image %u:            %s @ 0x%llx (0x%llx bytes)\n",
               index + 1, manifest.entries[index].name,
               (unsigned long long)manifest.entries[index].offset,
               (unsigned long long)manifest.entries[index].size);
    printf("  Output size:        0x%llx\n",
           (unsigned long long)manifest.header.imageSize);
    status = 0;

cleanup:
    free(primary.fileBuffer);
    free(shim.fileBuffer);
    free(loader.fileBuffer);
    free(output.fileBuffer);
    return status;
}