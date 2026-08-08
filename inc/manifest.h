#pragma once

#include "utils.h"

#include <stddef.h>
#include <stdint.h>

#define MANIFEST_MAGIC 0x464D504BUL // 'K', 'P', 'M', 'F'
#define MANIFEST_VERSION_MAJOR 0U
#define MANIFEST_VERSION_MINOR 1U
#define MANIFEST_MAX_ENTRIES 50U

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
  SHIM_ENTRY_FLAG_HAS_QC_ABOOT_HEADER  = BIT(4),
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
  uint64_t instructions;
  uint64_t imageSize;
  uint64_t manifestOffset;
  uint64_t manifestSize;
  uint64_t shimOffset;
} ManifestBaseImage;

_Static_assert(
    sizeof(ManifestBaseImage) == 5 * sizeof(uint64_t),
    "Invalid manifest base image size");

typedef struct {
  uint32_t          magic;
  ManifestVersion   version;
  uint32_t          entryCount;
  uint32_t          defaultEntry;
  uint32_t          timeoutMs;
  ManifestBaseImage baseImage;
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
} ManifestEntry;

_Static_assert(sizeof(ManifestEntry) == 84, "Invalid manifest entry size");

typedef struct {
  ManifestHeader header;
  ManifestEntry  entries[];
} Manifest;

#pragma pack()
