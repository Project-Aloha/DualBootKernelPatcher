/** @file
 * Multi-Boot Kernel Patcher entry point.
 *
 * Copyright (c) 2021-2026 The DuoWoa authors. All rights reserved.
 * Copyright (c) 2024-2026 The AlohaWoA authors. All rights reserved.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pack.h"

#include <errno.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    printf("Multi-Boot Kernel Patcher v2.0.0.0\n");
    printf("Copyright (c) The DuoWoA & The AlohaWoA authors\n");
    if (argc != 2) {
        printf("Usage: MultiBootKernelPatcher <Config File>\n");
        return -EINVAL;
    }
    return PackConfigFile(argv[1]);
}
