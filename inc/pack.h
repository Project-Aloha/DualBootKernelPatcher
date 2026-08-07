#pragma once

#include "manifest.h"

#include <stddef.h>
#include <stdint.h>

#define PACK_PATH_SIZE 4096U

typedef struct {
  char     section[32];
  char     name[32];
  char     path[PACK_PATH_SIZE];
  uint32_t type;
  int      hasType;
  int      baseImage;
  uint64_t copyAddress;
  int      hasCopyAddress;
  uint64_t copySizeMax;
  int      hasCopySizeMax;
  uint32_t alignment;
  int      hasAlignment;
  uint64_t entryOffset;
  int      hasEntryOffset;
  char     deviceTreeBlobSection[32];
  int      hasDeviceTreeBlob;
  uint32_t deviceTreeBlobEntry;
} PackImageConfig;

typedef struct {
  int      present;
  uint32_t type;
  int      hasType;
} PackManifestConfig;

typedef struct {
  char               shim[PACK_PATH_SIZE];
  char               output[PACK_PATH_SIZE];
  char               defaultSection[32];
  PackImageConfig    images[MANIFEST_MAX_ENTRIES - 1];
  size_t             imageCount;
  size_t             baseImageIndex;
  uint32_t           defaultEntry;
  uint32_t           timeoutMs;
  PackManifestConfig manifest;
} PackConfig;

int PackConfigFile(const char *configPath);
int PackImage(const PackConfig *config);
