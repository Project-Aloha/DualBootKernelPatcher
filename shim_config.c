#include "shim.h"

#include <ini.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    ShimPackConfig config;
    char configDirectory[SHIM_PATH_SIZE];
    char error[256];
} ConfigParser;

static bool copy_text(char *destination, size_t size, const char *value) {
    size_t length = strlen(value);
    if (length >= size)
        return false;
    memcpy(destination, value, length + 1);
    return true;
}

static bool resolve_path(char *destination,
                         size_t size,
                         const char *configDirectory,
                         const char *value) {
    if (value[0] == '/')
        return copy_text(destination, size, value);

    int written = snprintf(destination, size, "%s/%s", configDirectory, value);
    return written >= 0 && (size_t)written < size;
}

static bool parse_u32(const char *value, uint32_t *result) {
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(value, &end, 0);
    if (errno != 0 || value[0] == '\0' || *end != '\0' || parsed > UINT32_MAX)
        return false;
    *result = (uint32_t)parsed;
    return true;
}

static bool parse_u64(const char *value, uint64_t *result) {
    char *end = NULL;
    errno = 0;
    if (value[0] == '-')
        return false;
    unsigned long long parsed = strtoull(value, &end, 0);
    if (errno != 0 || value[0] == '\0' || *end != '\0')
        return false;
    *result = (uint64_t)parsed;
    return true;
}

static bool parse_bool(const char *value, int *result) {
    if (strcmp(value, "true") == 0 || strcmp(value, "yes") == 0 ||
        strcmp(value, "1") == 0) {
        *result = 1;
        return true;
    }
    if (strcmp(value, "false") == 0 || strcmp(value, "no") == 0 ||
        strcmp(value, "0") == 0) {
        *result = 0;
        return true;
    }
    return false;
}

static bool set_once(char *destination, size_t size, const char *value) {
    return destination[0] == '\0' && copy_text(destination, size, value);
}

static bool set_path_once(char *destination,
                          size_t size,
                          const ConfigParser *parser,
                          const char *value) {
    return destination[0] == '\0' &&
           resolve_path(destination, size, parser->configDirectory, value);
}

static ShimImageConfig *find_or_add_image(ConfigParser *parser,
                                          const char *section) {
    for (size_t index = 0; index < parser->config.imageCount; index++) {
        if (strcmp(parser->config.images[index].section, section) == 0)
            return &parser->config.images[index];
    }

    if (parser->config.imageCount >= SHIM_MAX_ENTRIES ||
        strlen(section) >= sizeof(parser->config.images[0].section))
        return NULL;

    ShimImageConfig *image =
        &parser->config.images[parser->config.imageCount++];
    memcpy(image->section, section, strlen(section) + 1);
    image->baseImage = -1;
    return image;
}

static int config_handler(void *user,
                          const char *section,
                          const char *name,
                          const char *value) {
    ConfigParser *parser = user;
    bool valid = false;

    if (strcmp(section, "Pack") == 0) {
        if (strcmp(name, "Shim") == 0)
            valid = set_path_once(parser->config.shim,
                                  sizeof(parser->config.shim), parser, value);
        else if (strcmp(name, "Output") == 0)
            valid = set_path_once(parser->config.output,
                                  sizeof(parser->config.output), parser, value);
        else if (strcmp(name, "Loader") == 0)
            valid = set_path_once(parser->config.loader,
                                  sizeof(parser->config.loader), parser, value);
        else if (strcmp(name, "Default") == 0)
            valid = set_once(parser->config.defaultSection,
                             sizeof(parser->config.defaultSection), value);
        else if (strcmp(name, "Timeout") == 0)
            valid = parse_u32(value, &parser->config.timeoutMs);
    } else if (strncmp(section, "Image-", 6) == 0 && section[6] != '\0') {
        ShimImageConfig *image = find_or_add_image(parser, section);
        if (image != NULL) {
            if (strcmp(name, "Name") == 0)
                valid = set_once(image->name, sizeof(image->name), value);
            else if (strcmp(name, "Path") == 0)
                valid = set_path_once(image->path, sizeof(image->path),
                                      parser, value);
            else if (strcmp(name, "BaseImage") == 0 && image->baseImage == -1)
                valid = parse_bool(value, &image->baseImage);
            else if (strcmp(name, "CopyTo") == 0 &&
                     !image->hasCopyAddress) {
                valid = parse_u64(value, &image->copyAddress);
                image->hasCopyAddress = valid;
            } else if (strcmp(name, "CopySizeMax") == 0 &&
                       !image->hasCopySizeMax) {
                valid = parse_u64(value, &image->copySizeMax);
                image->hasCopySizeMax = valid;
            } else if (strcmp(name, "Align") == 0 &&
                       !image->hasAlignment) {
                valid = parse_u64(value, &image->alignment);
                image->hasAlignment = valid;
            }
        }
    }

    if (!valid) {
        snprintf(parser->error, sizeof(parser->error),
                 "invalid or duplicate key [%s] %s", section, name);
        return 0;
    }
    return 1;
}

static bool get_config_directory(const char *configPath,
                                 char *directory,
                                 size_t size) {
    const char *separator = strrchr(configPath, '/');
    if (separator == NULL)
        return copy_text(directory, size, ".");

    size_t length = (size_t)(separator - configPath);
    if (length == 0)
        length = 1;
    if (length >= size)
        return false;
    memcpy(directory, configPath, length);
    directory[length] = '\0';
    return true;
}

static bool validate_config(ConfigParser *parser,
                            size_t *baseImageIndex,
                            size_t *defaultIndex) {
    ShimPackConfig *config = &parser->config;
    if (config->shim[0] == '\0' || config->output[0] == '\0' ||
        config->loader[0] == '\0' || config->defaultSection[0] == '\0') {
        printf("Error: [Pack] requires Shim, Output, Loader and Default.\n");
        return false;
    }
    if (config->imageCount == 0) {
        printf("Error: At least one [Image-*] section is required.\n");
        return false;
    }

    size_t baseImageCount = 0;
    bool defaultFound = false;
    for (size_t index = 0; index < config->imageCount; index++) {
        ShimImageConfig *image = &config->images[index];
        if (image->name[0] == '\0' || image->path[0] == '\0' ||
            image->baseImage == -1) {
            printf("Error: [%s] requires Name, Path and BaseImage.\n",
                   image->section);
            return false;
        }
        if (!image->hasAlignment)
            image->alignment = 4;
        if (image->alignment < 4 ||
            image->alignment > SHIM_KERNEL_ALIGNMENT ||
            (image->alignment & (image->alignment - 1)) != 0) {
            printf("Error: [%s] Align must be a power of two from 4 to "
                   "0x%x.\n", image->section, SHIM_KERNEL_ALIGNMENT);
            return false;
        }
        if (image->baseImage) {
            if (image->hasCopyAddress) {
                printf("Error: Base image [%s] cannot set CopyTo.\n",
                       image->section);
                return false;
            }
            *baseImageIndex = index;
            baseImageCount++;
        }
        if (image->hasCopyAddress) {
            if (image->copyAddress == 0) {
                printf("Error: [%s] CopyTo address must be non-zero.\n",
                       image->section);
                return false;
            }
            if ((image->copyAddress & 3) != 0) {
                printf("Error: [%s] CopyTo address must be aligned to 4 "
                       "bytes.\n", image->section);
                return false;
            }
            if (!image->hasCopySizeMax || image->copySizeMax == 0) {
                printf("Error: [%s] with CopyTo requires non-zero "
                       "CopySizeMax.\n", image->section);
                return false;
            }
            if (image->copyAddress > UINT64_MAX - image->copySizeMax) {
                printf("Error: [%s] CopyTo region overflows.\n",
                       image->section);
                return false;
            }
        } else if (image->hasCopySizeMax) {
            printf("Error: [%s] CopySizeMax requires CopyTo.\n",
                   image->section);
            return false;
        }
        if (strcmp(image->section, config->defaultSection) == 0) {
            *defaultIndex = index;
            defaultFound = true;
        }
    }

    if (baseImageCount != 1) {
        printf("Error: Exactly one [Image-*] must set BaseImage=true.\n");
        return false;
    }
    config->baseImageIndex = *baseImageIndex;
    if (!defaultFound) {
        printf("Error: Default must name an [Image-*] section.\n");
        return false;
    }
    config->defaultEntry = (uint32_t)(
        *defaultIndex == *baseImageIndex
            ? 0
            : 1 + *defaultIndex - (*defaultIndex > *baseImageIndex));
    return true;
}

int PackConfig(const char *configPath) {
    ConfigParser parser = {0};
    parser.config.timeoutMs = 5000;
    if (!get_config_directory(configPath, parser.configDirectory,
                              sizeof(parser.configDirectory))) {
        printf("Error: Config path is too long.\n");
        return -EINVAL;
    }

    int parseStatus = ini_parse(configPath, config_handler, &parser);
    if (parseStatus != 0) {
        if (parseStatus == -1)
            printf("Error: Cannot open config: %s\n", configPath);
        else
            printf("Error: Config line %d: %s\n", parseStatus,
                   parser.error[0] != '\0' ? parser.error : "parse failure");
        return -EINVAL;
    }

    size_t baseImageIndex = 0;
    size_t defaultIndex = SIZE_MAX;
    if (!validate_config(&parser, &baseImageIndex, &defaultIndex))
        return -EINVAL;

    return PackShim(&parser.config);
}