#pragma once

#include "manifest.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PACK_PATH_SIZE 4096U
#define CFG_NAME_LEN_MAX 64U

typedef enum {
  PACK_IMAGE_FIELD_TYPE             = BIT(0),
  PACK_IMAGE_FIELD_BASE_IMAGE       = BIT(1), // BaseImage=true
  PACK_IMAGE_FIELD_COPY_ADDRESS     = BIT(2),
  PACK_IMAGE_FIELD_COPY_SIZE_MAX    = BIT(3),
  PACK_IMAGE_FIELD_ALIGNMENT        = BIT(4),
  PACK_IMAGE_FIELD_ENTRY_OFFSET     = BIT(5),
  PACK_IMAGE_FIELD_DEVICE_TREE_BLOB = BIT(6),
  PACK_IMAGE_FIELD_BASE_IMAGE_SET   = BIT(7), // BaseImage key was present
} PackImageField;

// Metadata of an entry in the pack config.
typedef struct {
  char     section[CFG_NAME_LEN_MAX];
  char    *name;
  char     path[PACK_PATH_SIZE];
  char     dtbXref[CFG_NAME_LEN_MAX];
  uint64_t copyAddress;
  uint64_t copySizeMax;
  uint64_t entryOffset;
  uint32_t type;
  uint32_t alignment;
  uint32_t deviceTreeBlobEntry;
  uint32_t fieldsPresent;
} PackImageConfig;

typedef struct {
  uint32_t type;
  bool     present;
  bool     hasType;
} PackManifestConfig;

typedef struct {
  char               shim[PACK_PATH_SIZE];
  char               output[PACK_PATH_SIZE];
  char               defaultSection[CFG_NAME_LEN_MAX];
  PackImageConfig    images[MANIFEST_MAX_ENTRIES - 1];
  size_t             imageCount;
  size_t             baseImageIndex;
  uint32_t           defaultEntry;
  uint32_t           timeoutMs;
  PackManifestConfig manifest;
} PackConfig;

int PackConfigFile(const char *configPath);
int PackImage(const PackConfig *config);
