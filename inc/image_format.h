#pragma once

#include "utils.h"

#include <stdbool.h>

#define ARM64_LINUX_IMAGE_HEADER_SIZE 0x40U
#define ARM64_BRANCH_ALIGNMENT 4U
#define ARM64_BRANCH_MAX_OFFSET SIZE_MB(128)
#define QC_ABOOT_KERNEL_HDR_SIZE 0x14U

typedef struct {
  size_t   headerOffset;
  size_t   payloadSize;
  uint64_t textOffset;
  uint64_t imageSize;
} Arm64ImageInfo;

uint32_t image_read_le32(const uint8_t *buffer);
uint64_t image_read_le64(const uint8_t *buffer);
void     image_write_le32(uint8_t *buffer, uint32_t value);
void     image_write_le64(uint8_t *buffer, uint64_t value);
bool     image_align_up(size_t value, size_t alignment, size_t *result);
bool image_inspect_arm64_linux(const FileContent *file, Arm64ImageInfo *info);
bool manifest_entry_type_is_executable(uint32_t type);
const char *manifest_entry_type_name(uint32_t type);
