/** @file
 * Multi-Boot Kernel Patcher entry point.
 *
 * Copyright (c) 2021-2026 The DuoWoa authors. All rights reserved.
 * Copyright (c) 2024-2026 The AlohaWoA authors. All rights reserved.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "log.h"
#include "pack.h"

#include <errno.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
  log_info("Multi-Boot Kernel Patcher v2.0.0.0");
  log_info("Copyright (c) The DuoWoA & The AlohaWoA authors");
  if (argc != 2) {
    log_err("Usage: MultiBootKernelPatcher <Config File>");
    return -EINVAL;
  }
  return PackConfigFile(argv[1]);
}
