#pragma once

#include "utils.h"

#define MANIFEST_MAGIC 0x464D504BUL // 'K', 'P', 'M', 'F'

// The ABI is not frozen; development builds always report version 0.1.
#define MANIFEST_VERSION_MAJOR 0U
#define MANIFEST_VERSION_MINOR 1U

// Max entries supported in manifest
#define MANIFEST_MAX_ENTRIES 50U

// We can only ensure previous bootloader load
// the patched image at 2MB alignment.
#define MANIFEST_ALIGNMENT_SHIFT_MIN 2U  // 4B
#define MANIFEST_ALIGNMENT_SHIFT_MAX 21U // 2MB
#define MANIFEST_ENTRY_ALIGNMENT 4U

// Entry types in config.
typedef enum {
  MF_ENTRY_TYPE_LINUX = 1,
  MF_ENTRY_TYPE_FREE_EXEC,
  MF_ENTRY_TYPE_SHIM,
  MF_ENTRY_TYPE_BLOB,
  MF_ENTRY_TYPE_DTB,
  MF_ENTRY_TYPE_MANIFEST,
} ManifestEntryType;

// Bitmap flags.
typedef enum {
  MF_ENTRY_FLAG_BASE_IMAGE           = BIT(0),
  MF_ENTRY_FLAG_COPY                 = BIT(1),
  MF_ENTRY_FLAG_EXECUTABLE           = BIT(2),
  MF_ENTRY_FLAG_HAS_DEVICE_TREE_BLOB = BIT(3),
  MF_ENTRY_FLAG_HAS_QC_ABOOT_HEADER  = BIT(4),
} ManifestEntryFlag;

typedef struct {
  uint16_t major;
  uint16_t minor;
} ManifestVersion;

_Static_assert(
    sizeof(ManifestVersion) == sizeof(uint32_t),
    "Invalid manifest version size");

#pragma pack(1)

// Backup of the original base-image's header
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

/* Manifest blob data structure */
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
  uint64_t offset;
  uint64_t size;
  uint32_t type;
  uint32_t flags;
  uint64_t loadAddress;
  uint32_t entryOffset;
  uint32_t deviceTreeBlobEntry;
  uint32_t alignment;
  uint64_t copySizeMax;
  uint32_t nameLen;
  char     name[];
} ManifestEntry;

_Static_assert(sizeof(ManifestEntry) == 56, "Invalid manifest entry size");

typedef struct {
  ManifestHeader header;
  uint8_t        entries[];
} Manifest;

#pragma pack()
