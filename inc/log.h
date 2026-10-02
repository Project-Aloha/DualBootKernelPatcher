#pragma once

#include <stdio.h>

#define LOG_COLOR_RESET "\x1b[0m"
#define LOG_COLOR_INFO "\x1b[97m"       /* bright white */
#define LOG_COLOR_WARN "\x1b[38;5;208m" /* orange (256-color) */
#define LOG_COLOR_ERROR "\x1b[31m"      /* red */
#define LOG_COLOR_OK "\x1b[32m"         /* green */
#define LOG_COLOR_DIM "\x1b[2m"         /* faint */

#define log_raw(fmt, ...) printf(fmt LOG_COLOR_RESET, ##__VA_ARGS__)

#define log_info(fmt, ...)                                                     \
  printf(LOG_COLOR_INFO "[INFO] " fmt LOG_COLOR_RESET "\n", ##__VA_ARGS__)

#define log_warn(fmt, ...)                                                     \
  printf(LOG_COLOR_WARN "[WARN] " fmt LOG_COLOR_RESET "\n", ##__VA_ARGS__)

#define log_err(fmt, ...)                                                      \
  printf(LOG_COLOR_ERROR "[ERROR] " fmt LOG_COLOR_RESET "\n", ##__VA_ARGS__)

#define log_ok(fmt, ...)                                                       \
  printf(LOG_COLOR_OK "[OK] " fmt LOG_COLOR_RESET "\n", ##__VA_ARGS__)
