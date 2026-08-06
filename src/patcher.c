/** @file
 * Multi-Boot Kernel Patcher entry point.
 *
 * Copyright (c) 2021-2025 The DuoWoa authors. All rights reserved.
 * MIT License
 */

#include "shim.h"

#include <errno.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    printf("Project Aloha Multi-Boot Kernel Patcher v1.2.0.0\n");
    printf("Copyright (c) 2021-2025 The DuoWoA authors\n\n");
    if (argc != 2) {
        printf("Usage: MultiBootKernelPatcher <Config File>\n");
        return -EINVAL;
    }
    return PackConfig(argv[1]);
}
