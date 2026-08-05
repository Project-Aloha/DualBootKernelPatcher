#pragma once

#include <stddef.h>
#include <stdint.h>

#define SHIM_MANIFEST_MAGIC "SHIMMF\0"
#define SHIM_MANIFEST_VERSION 7U
#define SHIM_MANIFEST_MAX_SIZE 0x2000U
#define SHIM_MAX_ENTRIES 50U
#define SHIM_NO_DEVICE_TREE_BLOB 0U
#define SHIM_LINUX_HEADER_SIZE 0x40U
#define SHIM_WRAPPER_SIZE 0x14U

#define SHIM_ENTRY_TYPE_LINUX 1U
#define SHIM_ENTRY_TYPE_FREE_EXEC 2U
#define SHIM_ENTRY_TYPE_SHIM 3U
#define SHIM_ENTRY_TYPE_BLOB 4U
#define SHIM_ENTRY_TYPE_DTB 5U
#define SHIM_ENTRY_TYPE_MANIFEST 6U
#define SHIM_ENTRY_FLAG_BASE_IMAGE 1U
#define SHIM_ENTRY_FLAG_COPY 2U

#ifdef _MSC_VER
#pragma pack(push, 1)
#define SHIM_PACKED
#else
#define SHIM_PACKED __attribute__((packed))
#endif

typedef struct SHIM_PACKED {
    char magic[8];
    uint32_t version;
    uint32_t headerSize;
    uint32_t entrySize;
    uint32_t entryCount;
    uint32_t defaultEntry;
    uint32_t timeoutMs;
    uint64_t imageSize;
    uint64_t shimSize;
    uint8_t baseImageHeader[SHIM_LINUX_HEADER_SIZE];
    uint8_t reserved[16];
} ShimManifestHeader;

typedef struct SHIM_PACKED {
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
    uint8_t sourcePrefix[SHIM_WRAPPER_SIZE];
    uint8_t reserved[8];
} ShimManifestEntry;

typedef struct SHIM_PACKED {
    ShimManifestHeader header;
    ShimManifestEntry entries[];
} ShimManifest;

typedef struct SHIM_PACKED {
    ShimManifestHeader header;
    ShimManifestEntry entries[SHIM_MAX_ENTRIES];
} ShimManifestStorage;

#ifdef _MSC_VER
#pragma pack(pop)
#endif
#undef SHIM_PACKED

_Static_assert(sizeof(ShimManifestHeader) == 128,
               "Invalid Shim manifest header size");
_Static_assert(sizeof(ShimManifestEntry) == 128,
               "Invalid Shim manifest entry size");
_Static_assert(sizeof(ShimManifestStorage) <= SHIM_MANIFEST_MAX_SIZE,
               "Shim manifest exceeds its maximum size");
