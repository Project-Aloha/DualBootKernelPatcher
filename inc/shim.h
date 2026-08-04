#pragma once

#include <stddef.h>
#include <stdint.h>

#define SHIM_MANIFEST_MAGIC "SHIMMF\0"
#define SHIM_MANIFEST_VERSION 3U
#define SHIM_MANIFEST_MAX_SIZE 0x1000U
#define SHIM_MAX_ENTRIES 63U
#define SHIM_KERNEL_ALIGNMENT 0x200000U
#define SHIM_LOADER_MAGIC "SHIMLDR"
#define SIMPLE_SHIM_MAGIC "SHIMASM"
#define SIMPLE_SHIM_PAYLOAD_OFFSET 0x20U
#define SIMPLE_SHIM_COPY_ADDRESS_OFFSET 0x28U
#define SIMPLE_SHIM_COPY_SIZE_OFFSET 0x30U

#define SHIM_ENTRY_TYPE_LINUX 1U
#define SHIM_ENTRY_TYPE_EXECUTABLE 2U
#define SHIM_ENTRY_FLAG_BASE_IMAGE 1U
#define SHIM_ENTRY_FLAG_COPY 2U

#define SHIM_PATH_SIZE 4096U

typedef struct {
    char section[32];
    char name[32];
    char path[SHIM_PATH_SIZE];
    int baseImage;
    uint64_t copyAddress;
    int hasCopyAddress;
    uint64_t copySizeMax;
    int hasCopySizeMax;
    uint64_t alignment;
    int hasAlignment;
} ShimImageConfig;

typedef struct {
    char shim[SHIM_PATH_SIZE];
    char output[SHIM_PATH_SIZE];
    char loader[SHIM_PATH_SIZE];
    char defaultSection[32];
    ShimImageConfig images[SHIM_MAX_ENTRIES];
    size_t imageCount;
    size_t baseImageIndex;
    uint32_t defaultEntry;
    uint32_t timeoutMs;
} ShimPackConfig;

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
    uint64_t reserved[3];
} ShimManifestHeader;

typedef struct SHIM_PACKED {
    char name[32];
    uint64_t offset;
    uint64_t size;
    uint32_t type;
    uint32_t flags;
    uint64_t loadAddress;
} ShimManifestEntry;

typedef struct SHIM_PACKED {
    ShimManifestHeader header;
    ShimManifestEntry entries[SHIM_MAX_ENTRIES];
} ShimManifest;

#ifdef _MSC_VER
#pragma pack(pop)
#endif
#undef SHIM_PACKED

_Static_assert(sizeof(ShimManifestHeader) == 64,
               "Invalid Shim manifest header size");
_Static_assert(sizeof(ShimManifestEntry) == 64,
               "Invalid Shim manifest entry size");
_Static_assert(sizeof(ShimManifest) == SHIM_MANIFEST_MAX_SIZE,
               "Invalid Shim manifest size");

int PackConfig(const char *configPath);
int PackShim(const ShimPackConfig *config);