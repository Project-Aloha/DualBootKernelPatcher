#include "image_format.h"
#include "log.h"
#include "pack.h"

#include <ini.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  PackConfig config;
  char       configDirectory[PACK_PATH_SIZE];
  char       error[256];
} ConfigParser;

static bool copy_text(char *destination, size_t size, const char *value)
{
  size_t length = strlen(value);
  if (length >= size)
    return false;
  memcpy(destination, value, length + 1);
  return true;
}

static bool resolve_path(
    char *destination, size_t size, const char *configDirectory,
    const char *value)
{
  if (value[0] == '/')
    return copy_text(destination, size, value);

  int written = snprintf(destination, size, "%s/%s", configDirectory, value);
  return written >= 0 && (size_t)written < size;
}

static bool parse_u32(const char *value, uint32_t *result)
{
  char *end            = NULL;
  errno                = 0;
  unsigned long parsed = strtoul(value, &end, 0);
  if (errno != 0 || value[0] == '\0' || *end != '\0' || parsed > UINT32_MAX)
    return false;
  *result = (uint32_t)parsed;
  return true;
}

static bool parse_u64(const char *value, uint64_t *result)
{
  char *end = NULL;
  errno     = 0;
  if (value[0] == '-')
    return false;
  unsigned long long parsed = strtoull(value, &end, 0);
  if (errno != 0 || value[0] == '\0' || *end != '\0')
    return false;
  *result = (uint64_t)parsed;
  return true;
}

static bool parse_bool(const char *value, int *result)
{
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

static bool parse_image_type(const char *value, uint32_t *result)
{
  static const struct {
    const char *name;
    uint32_t    type;
  } types[] = {
      {"Linux", SHIM_ENTRY_TYPE_LINUX}, {"FreeExec", SHIM_ENTRY_TYPE_FREE_EXEC},
      {"Shim", SHIM_ENTRY_TYPE_SHIM},   {"Blob", SHIM_ENTRY_TYPE_BLOB},
      {"DTB", SHIM_ENTRY_TYPE_DTB},     {"Manifest", SHIM_ENTRY_TYPE_MANIFEST},
  };

  for (size_t index = 0; index < sizeof(types) / sizeof(types[0]); index++) {
    if (strcmp(value, types[index].name) == 0) {
      *result = types[index].type;
      return true;
    }
  }
  return false;
}

static bool set_once(char *destination, size_t size, const char *value)
{
  return destination[0] == '\0' && copy_text(destination, size, value);
}

static bool set_path_once(
    char *destination, size_t size, const ConfigParser *parser,
    const char *value)
{
  return destination[0] == '\0' &&
         resolve_path(destination, size, parser->configDirectory, value);
}

static PackImageConfig *
find_or_add_image(ConfigParser *parser, const char *section)
{
  for (size_t index = 0; index < parser->config.imageCount; index++) {
    if (strcmp(parser->config.images[index].section, section) == 0)
      return &parser->config.images[index];
  }

  if (parser->config.imageCount >= MANIFEST_MAX_ENTRIES - 1 ||
      strlen(section) >= sizeof(parser->config.images[0].section))
    return NULL;

  PackImageConfig *image = &parser->config.images[parser->config.imageCount++];
  memcpy(image->section, section, strlen(section) + 1);
  image->baseImage = -1;
  return image;
}

static uint32_t manifest_entry_index(size_t imageIndex, size_t baseImageIndex)
{
  if (imageIndex == baseImageIndex)
    return 0;
  return (uint32_t)(1 + imageIndex - (imageIndex > baseImageIndex));
}

static int config_handler(
    void *user, const char *section, const char *name, const char *value)
{
  ConfigParser *parser = user;
  bool          valid  = false;

  if (parser->config.manifest.present && strcmp(section, "Manifest") != 0) {
    snprintf(
        parser->error, sizeof(parser->error),
        "[Manifest] must be the final section");
    return 0;
  }

  if (strcmp(section, "Pack") == 0) {
    if (strcmp(name, "Shim") == 0)
      valid = set_path_once(
          parser->config.shim, sizeof(parser->config.shim), parser, value);
    else if (strcmp(name, "Output") == 0)
      valid = set_path_once(
          parser->config.output, sizeof(parser->config.output), parser, value);
    else if (strcmp(name, "Default") == 0)
      valid = set_once(
          parser->config.defaultSection, sizeof(parser->config.defaultSection),
          value);
    else if (strcmp(name, "Timeout") == 0)
      valid = parse_u32(value, &parser->config.timeoutMs);
  }
  else if (strcmp(section, "Manifest") == 0) {
    parser->config.manifest.present = 1;
    if (strcmp(name, "Type") == 0 && !parser->config.manifest.hasType) {
      valid = parse_image_type(value, &parser->config.manifest.type);
      parser->config.manifest.hasType = valid;
    }
  }
  else if (
      strcmp(section, "Image") == 0 ||
      (strncmp(section, "Image-", 6) == 0 && section[6] != '\0')) {
    PackImageConfig *image = find_or_add_image(parser, section);
    if (image != NULL) {
      if (strcmp(name, "Name") == 0)
        valid = set_once(image->name, sizeof(image->name), value);
      else if (strcmp(name, "Path") == 0)
        valid = set_path_once(image->path, sizeof(image->path), parser, value);
      else if (strcmp(name, "Type") == 0 && !image->hasType) {
        valid          = parse_image_type(value, &image->type);
        image->hasType = valid;
      }
      else if (strcmp(name, "BaseImage") == 0 && image->baseImage == -1)
        valid = parse_bool(value, &image->baseImage);
      else if (strcmp(name, "CopyTo") == 0 && !image->hasCopyAddress) {
        valid                 = parse_u64(value, &image->copyAddress);
        image->hasCopyAddress = valid;
      }
      else if (strcmp(name, "CopySizeMax") == 0 && !image->hasCopySizeMax) {
        valid                 = parse_u64(value, &image->copySizeMax);
        image->hasCopySizeMax = valid;
      }
      else if (strcmp(name, "Align") == 0 && !image->hasAlignment) {
        valid               = parse_u32(value, &image->alignment);
        image->hasAlignment = valid;
      }
      else if (strcmp(name, "EntryOffset") == 0 && !image->hasEntryOffset) {
        valid                 = parse_u64(value, &image->entryOffset);
        image->hasEntryOffset = valid;
      }
      else if (
          strcmp(name, "DeviceTreeBlob") == 0 && !image->hasDeviceTreeBlob &&
          value[0] != '\0') {
        valid = copy_text(
            image->deviceTreeBlobSection, sizeof(image->deviceTreeBlobSection),
            value);
        image->hasDeviceTreeBlob = valid;
      }
    }
  }

  if (!valid) {
    snprintf(
        parser->error, sizeof(parser->error),
        "invalid or duplicate key [%s] %s", section, name);
    return 0;
  }
  return 1;
}

static bool
get_config_directory(const char *configPath, char *directory, size_t size)
{
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

static bool validate_config(
    ConfigParser *parser, size_t *baseImageIndex, size_t *defaultIndex)
{
  PackConfig *config = &parser->config;
  if (config->shim[0] == '\0' || config->output[0] == '\0') {
    log_err("[Pack] requires Shim and Output.");
    return false;
  }
  if (config->imageCount == 0) {
    log_err("At least one [Image-*] section is required.");
    return false;
  }

  size_t baseImageCount             = 0;
  bool   defaultFound               = false;
  bool   hasDeviceTreeBlobReference = false;
  for (size_t index = 0; index < config->imageCount; index++) {
    PackImageConfig *image = &config->images[index];
    if (image->name[0] == '\0' || image->path[0] == '\0' || !image->hasType ||
        image->baseImage == -1) {
      log_err("[%s] requires Name, Path, Type and BaseImage.", image->section);
      return false;
    }
    if (!image->hasAlignment)
      image->alignment = 2;
    if (image->alignment < 2 || image->alignment > 31) {
      log_err("[%s] Align must be from 2 to 31.", image->section);
      return false;
    }
    if (image->baseImage) {
      if (image->type != SHIM_ENTRY_TYPE_LINUX) {
        log_err("Base image [%s] must set Type=Linux.", image->section);
        return false;
      }
      if (image->hasCopyAddress) {
        log_err("Base image [%s] cannot set CopyTo.", image->section);
        return false;
      }
      if (image->entryOffset != 0) {
        log_err("Base image [%s] cannot set EntryOffset.", image->section);
        return false;
      }
      *baseImageIndex = index;
      baseImageCount++;
    }
    if (image->hasCopyAddress) {
      if (image->copyAddress == 0) {
        log_err("[%s] CopyTo address must be non-zero.", image->section);
        return false;
      }
      if ((image->copyAddress & 3) != 0) {
        log_err(
            "[%s] CopyTo address must be aligned to 4 "
            "bytes.",
            image->section);
        return false;
      }
      if (!image->hasCopySizeMax || image->copySizeMax == 0) {
        log_err(
            "[%s] with CopyTo requires non-zero "
            "CopySizeMax.",
            image->section);
        return false;
      }
      if (image->copyAddress > UINT64_MAX - image->copySizeMax) {
        log_err("[%s] CopyTo region overflows.", image->section);
        return false;
      }
    }
    else if (image->hasCopySizeMax) {
      log_err("[%s] CopySizeMax requires CopyTo.", image->section);
      return false;
    }
    bool executable = manifest_entry_type_is_executable(image->type);
    if (image->type == SHIM_ENTRY_TYPE_MANIFEST) {
      log_err(
          "[%s] must be configured as the [Manifest] "
          "section.",
          image->section);
      return false;
    }
    if (!executable && image->hasEntryOffset) {
      log_err("[%s] Type does not allow EntryOffset.", image->section);
      return false;
    }
    if (image->hasDeviceTreeBlob && !executable) {
      log_err(
          "[%s] DeviceTreeBlob requires an executable Type.", image->section);
      return false;
    }
    hasDeviceTreeBlobReference |= image->hasDeviceTreeBlob;
    if (config->defaultSection[0] != '\0' &&
        strcmp(image->section, config->defaultSection) == 0) {
      *defaultIndex = index;
      defaultFound  = true;
    }
  }

  if (config->manifest.present &&
      (!config->manifest.hasType ||
       config->manifest.type != SHIM_ENTRY_TYPE_MANIFEST)) {
    log_err("[Manifest] must set Type=Manifest.");
    return false;
  }

  if (baseImageCount != 1) {
    log_err("Exactly one [Image-*] must set BaseImage=true.");
    return false;
  }
  config->baseImageIndex = *baseImageIndex;
  if (hasDeviceTreeBlobReference && !config->manifest.present) {
    log_err("DeviceTreeBlob requires a [Manifest] section.");
    return false;
  }
  for (size_t index = 0; index < config->imageCount; index++) {
    PackImageConfig *image = &config->images[index];
    if (!image->hasDeviceTreeBlob)
      continue;

    bool found = false;
    for (size_t targetIndex = 0; targetIndex < config->imageCount;
         targetIndex++) {
      PackImageConfig *target = &config->images[targetIndex];
      if (strcmp(target->section, image->deviceTreeBlobSection) != 0)
        continue;
      if (target->type != SHIM_ENTRY_TYPE_DTB) {
        log_err(
            "[%s] DeviceTreeBlob [%s] must set Type=DTB.", image->section,
            target->section);
        return false;
      }
      if (target->hasCopyAddress) {
        log_err(
            "[%s] DeviceTreeBlob [%s] cannot set CopyTo.", image->section,
            target->section);
        return false;
      }
      image->deviceTreeBlobEntry =
          manifest_entry_index(targetIndex, *baseImageIndex);
      found = true;
      break;
    }
    if (!found) {
      log_err(
          "[%s] DeviceTreeBlob section [%s] was not found.", image->section,
          image->deviceTreeBlobSection);
      return false;
    }
  }
  if (config->defaultSection[0] == '\0') {
    *defaultIndex = *baseImageIndex;
    defaultFound  = true;
  }
  if (!defaultFound) {
    log_err("Default must name an [Image-*] section.");
    return false;
  }
  if (!manifest_entry_type_is_executable(config->images[*defaultIndex].type)) {
    log_err("Default must name an executable [Image-*] section.");
    return false;
  }
  config->defaultEntry = manifest_entry_index(*defaultIndex, *baseImageIndex);
  return true;
}

int PackConfigFile(const char *configPath)
{
  ConfigParser parser     = {0};
  parser.config.timeoutMs = 5000;
  if (!get_config_directory(
          configPath, parser.configDirectory, sizeof(parser.configDirectory))) {
    log_err("Config path is too long.");
    return -EINVAL;
  }

  int parseStatus = ini_parse(configPath, config_handler, &parser);
  if (parseStatus != 0) {
    if (parseStatus == -1)
      log_err("Cannot open config: %s", configPath);
    else
      log_err(
          "Config line %d: %s", parseStatus,
          parser.error[0] != '\0' ? parser.error : "parse failure");
    return -EINVAL;
  }

  size_t baseImageIndex = 0;
  size_t defaultIndex   = SIZE_MAX;
  if (!validate_config(&parser, &baseImageIndex, &defaultIndex))
    return -EINVAL;

  return PackImage(&parser.config);
}