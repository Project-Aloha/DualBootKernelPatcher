#pragma once

#include "utils.h"

#include <stddef.h>
#include <stdint.h>

#define MANIFEST_MAGIC 0x464D504BUL // 'K', 'P', 'M', 'F'
#define MANIFEST_VERSION_MAJOR 0U
#define MANIFEST_VERSION_MINOR 1U
#define MANIFEST_MAX_ENTRIES 50U
#define MANIFEST_SOURCE_PREFIX_MAX_SIZE 0x14U

typedef enum {
  SHIM_ENTRY_TYPE_LINUX = 1,
  SHIM_ENTRY_TYPE_FREE_EXEC,
  SHIM_ENTRY_TYPE_SHIM,
  SHIM_ENTRY_TYPE_BLOB,
  SHIM_ENTRY_TYPE_DTB,
  SHIM_ENTRY_TYPE_MANIFEST,
} ManifestEntryType;

typedef enum {
  SHIM_ENTRY_FLAG_BASE_IMAGE           = BIT(0),
  SHIM_ENTRY_FLAG_COPY                 = BIT(1),
  SHIM_ENTRY_FLAG_EXECUTABLE           = BIT(2),
  SHIM_ENTRY_FLAG_HAS_DEVICE_TREE_BLOB = BIT(3),
} ManifestEntryFlag;

typedef struct {
  uint16_t major;
  uint16_t minor;
} ManifestVersion;

_Static_assert(
    sizeof(ManifestVersion) == sizeof(uint32_t),
    "Invalid manifest version size");

#pragma pack(1)

typedef struct {
  uint32_t        magic;
  ManifestVersion version;
  uint32_t        entryCount;
  uint32_t        defaultEntry;
  uint32_t        timeoutMs;
  uint64_t        baseImageInstructions;
  uint64_t        baseImageImageSize;
  uint64_t        baseImageManifestOffset;
  uint64_t        baseImageManifestSize;
  uint64_t        baseImageShimOffset;
} ManifestHeader;

_Static_assert(sizeof(ManifestHeader) == 60, "Invalid manifest header size");

typedef struct {
  char     name[32];
  uint64_t offset;
  uint64_t size;
  uint32_t type;
  uint32_t flags;
  uint64_t loadAddress;
  uint32_t entryOffset;
  uint32_t deviceTreeBlobEntry;
  uint32_t alignment;
  uint64_t copySizeMax;
  uint32_t sourcePrefixSize;
  uint8_t  sourcePrefix[MANIFEST_SOURCE_PREFIX_MAX_SIZE];
} ManifestEntry;

_Static_assert(sizeof(ManifestEntry) == 108, "Invalid manifest entry size");

typedef struct {
  ManifestHeader header;
  ManifestEntry  entries[];
} Manifest;

#pragma pack()
