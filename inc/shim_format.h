#pragma once

#include "shim.h"
#include "utils.h"

#include <stdbool.h>

typedef struct {
    size_t headerOffset;
    size_t payloadSize;
    uint64_t textOffset;
    uint64_t imageSize;
} Arm64ImageInfo;

uint32_t shim_read_le32(const uint8_t *buffer);
uint64_t shim_read_le64(const uint8_t *buffer);
void shim_write_le32(uint8_t *buffer, uint32_t value);
void shim_write_le64(uint8_t *buffer, uint64_t value);
bool shim_align_up(size_t value, size_t alignment, size_t *result);
bool shim_inspect_arm64_image(const FileContent *file, Arm64ImageInfo *info);
bool shim_is_executable_type(uint32_t type);
const char *shim_type_name(uint32_t type);
