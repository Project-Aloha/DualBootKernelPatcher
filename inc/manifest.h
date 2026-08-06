#pragma once

#include "utils.h"

#include <stddef.h>
#include <stdint.h>

#define MANIFEST_MAGIC "SHIMMF\0"
#define MANIFEST_VERSION_MAJOR 0U
#define MANIFEST_VERSION_MINOR 2U
#define MANIFEST_MAX_SIZE 0x2000U
#define MANIFEST_MAX_ENTRIES 50U
#define MANIFEST_BASE_IMAGE_HEADER_SIZE 0x40U
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
    SHIM_ENTRY_FLAG_BASE_IMAGE = BIT(0),
    SHIM_ENTRY_FLAG_COPY = BIT(1),
    SHIM_ENTRY_FLAG_EXECUTABLE = BIT(2),
    SHIM_ENTRY_FLAG_HAS_DEVICE_TREE_BLOB = BIT(3),
} ManifestEntryFlag;

typedef struct {
    uint16_t major;
    uint16_t minor;
} ManifestVersion;

_Static_assert(sizeof(ManifestVersion) == sizeof(uint32_t),
               "Invalid manifest version size");

#pragma pack(1)

typedef struct {
    char magic[8];
    ManifestVersion version;
    uint32_t headerSize;
    uint32_t entrySize;
    uint32_t entryCount;
    uint32_t defaultEntry;
    uint32_t timeoutMs;
    uint64_t imageSize;
    uint64_t shimSize;
    uint8_t baseImageHeader[MANIFEST_BASE_IMAGE_HEADER_SIZE];
    uint8_t reserved[16];
} ManifestHeader;

_Static_assert(sizeof(ManifestHeader) == 128,
               "Invalid manifest header size");

typedef struct {
    char name[32];
    uint64_t offset;
    uint64_t size;
    uint32_t type;
    uint32_t flags;
    uint64_t loadAddress;
    uint64_t entryOffset;
    uint64_t deviceTreeBlobEntry;
    uint64_t alignment;
    uint64_t copySizeMax;
    uint32_t sourcePrefixSize;
    uint8_t sourcePrefix[MANIFEST_SOURCE_PREFIX_MAX_SIZE];
    uint8_t reserved[8];
} ManifestEntry;

_Static_assert(sizeof(ManifestEntry) == 128,
               "Invalid manifest entry size");

typedef struct {
    ManifestHeader header;
    ManifestEntry entries[];
} Manifest;

typedef struct {
    ManifestHeader header;
    ManifestEntry entries[MANIFEST_MAX_ENTRIES];
} ManifestStorage;

_Static_assert(sizeof(ManifestStorage) <= MANIFEST_MAX_SIZE,
               "Manifest exceeds its maximum size");

#pragma pack()
