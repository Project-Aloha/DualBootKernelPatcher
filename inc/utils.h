/** @file
 * Multi-Boot Kernel Patcher Header File.
 *
 *  Copyright (c) 2021-2025 The DuoWoa authors. All rights reserved.
 *  MIT License
 *
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

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
