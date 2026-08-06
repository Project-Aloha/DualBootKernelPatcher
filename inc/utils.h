/** @file
 * Multi-Boot Kernel Patcher Header File.
 *
 *  Copyright (c) 2021-2026 The DuoWoa authors. All rights reserved.
 *  Copyright (c) 2024-2026 The AlohaWoA authors. All rights reserved.
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

// Sizes
#define SIZE_1B 1ULL
#define SIZE_1KB (1024ULL * SIZE_1B)
#define SIZE_1MB (1024ULL * SIZE_1KB)
#define SIZE_B(x) ((x) * SIZE_1B)
#define SIZE_KB(x) ((x) * SIZE_1KB)
#define SIZE_MB(x) ((x) * SIZE_1MB)

// Bitops
#define BIT(x) (1ULL << (x))

//
// Store some file information and file buffer.
//
typedef struct {
    uint8_t *fileBuffer;
    size_t fileSize;
    const char *filePath;
} FileContent, *pFileContent;

size_t get_file_size(FileContent *fileContent);

uint8_t *read_file_content(FileContent *fileContent);

int write_file_content(pFileContent fileContent);
