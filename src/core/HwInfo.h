#pragma once

/**
 * @file HwInfo.h
 * @brief ESP32-S3 module identification from eFuse BLK1 and flash JEDEC ID.
 */

#include <ArduinoJson.h>

/**
 * @brief Module identification without esptool: eFuse BLK1 (the fields esptool prints as
 * "Embedded PSRAM 8MB (AP_3v3)") plus the JEDEC ID of the flash chip. Works while PSRAM
 * is disabled in the build. Read once in begin(): the flash ID access briefly stalls
 * the caches, which must not happen while USB MSC is serving reads.
 */
namespace HwInfo {

/**
 * @brief Read eFuse BLK1 and the flash JEDEC ID once and log the result to Serial.
 *
 * Real flash size comes from the JEDEC capacity byte (0x10..0x1A), else from the eFuse
 * flash capacity. Subsequent calls are no-ops.
 * @warning Briefly stalls the caches (flash ID read); call before USB MSC serves reads.
 */
void begin();

/**
 * @brief Return the guessed module variant, e.g. "N16R8"; "?" if the flash size could not be determined.
 * @return Static string "N<flash MiB>R<psram MiB>" or "N<flash MiB>" without PSRAM.
 */
const char* moduleGuess();

/** @brief In-package PSRAM size in bytes according to eFuse (0 if none/unknown), independent of the build. */
uint32_t psramBytesEfuse();

/** @brief Physical flash size in bytes (JEDEC, fallback eFuse; 0 if unknown or begin() not called). */
uint32_t flashBytesReal();

/**
 * @brief Serialize module, package, flash, PSRAM and raw eFuse fields into a JSON object.
 *
 * Includes configured vs. real flash size, bytes_unused, and the PSRAM memory_type needed
 * in the build ("qio_qspi" / "qio_opi").
 *
 * @param o  Target JSON object.
 */
void toJson(JsonObject o);

}  // namespace HwInfo
