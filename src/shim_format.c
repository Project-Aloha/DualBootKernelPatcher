#include "shim_format.h"

uint32_t shim_read_le32(const uint8_t *buffer) {
    uint32_t value = 0;
    for (int index = 3; index >= 0; index--)
        value = (value << 8) | buffer[index];
    return value;
}

uint64_t shim_read_le64(const uint8_t *buffer) {
    uint64_t value = 0;
    for (int index = 7; index >= 0; index--)
        value = (value << 8) | buffer[index];
    return value;
}

void shim_write_le32(uint8_t *buffer, uint32_t value) {
    for (int index = 0; index < 4; index++)
        buffer[index] = (uint8_t)(value >> (index * 8) & 0xff);
}

void shim_write_le64(uint8_t *buffer, uint64_t value) {
    for (int index = 0; index < 8; index++)
        buffer[index] = (uint8_t)(value >> (index * 8) & 0xff);
}

bool shim_align_up(size_t value, size_t alignment, size_t *result) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
        value > SIZE_MAX - (alignment - 1))
        return false;
    *result = (value + alignment - 1) & ~(alignment - 1);
    return true;
}

bool shim_inspect_arm64_image(const FileContent *file, Arm64ImageInfo *info) {
    static const uint8_t arm64Magic[] = {'A', 'R', 'M', 0x64};

    info->headerOffset = 0;
    if (file->fileSize >= SHIM_WRAPPER_SIZE &&
        memcmp(file->fileBuffer, "UNCOMPRESSED_IMG", 0x10) == 0)
        info->headerOffset = SHIM_WRAPPER_SIZE;

    if (file->fileSize < info->headerOffset + SHIM_LINUX_HEADER_SIZE ||
        memcmp(file->fileBuffer + info->headerOffset + 0x38,
               arm64Magic, sizeof(arm64Magic)) != 0)
        return false;

    info->payloadSize = file->fileSize - info->headerOffset;
    info->textOffset = shim_read_le64(
        file->fileBuffer + info->headerOffset + 0x08);
    info->imageSize = shim_read_le64(
        file->fileBuffer + info->headerOffset + 0x10);
    return true;
}

bool shim_is_executable_type(uint32_t type) {
    return type == SHIM_ENTRY_TYPE_LINUX ||
           type == SHIM_ENTRY_TYPE_FREE_EXEC ||
           type == SHIM_ENTRY_TYPE_SHIM;
}

const char *shim_type_name(uint32_t type) {
    switch (type) {
    case SHIM_ENTRY_TYPE_LINUX:
        return "Linux";
    case SHIM_ENTRY_TYPE_FREE_EXEC:
        return "FreeExec";
    case SHIM_ENTRY_TYPE_SHIM:
        return "Shim";
    case SHIM_ENTRY_TYPE_BLOB:
        return "Blob";
    case SHIM_ENTRY_TYPE_DTB:
        return "DTB";
    case SHIM_ENTRY_TYPE_MANIFEST:
        return "Manifest";
    default:
        return NULL;
    }
}