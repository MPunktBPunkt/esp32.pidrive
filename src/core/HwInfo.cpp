/**
 * @file HwInfo.cpp
 * @brief eFuse BLK1 decoding and flash JEDEC lookup for HwInfo.
 */
#include "core/HwInfo.h"
#include <Arduino.h>
#include <esp_flash.h>
#include <soc/soc.h>

namespace {

/**
 * @brief Read word n of eFuse BLK1.
 *
 * EFUSE_RD_MAC_SPI_SYS_n_REG (BLK1); bit layout as in esptool targets/esp32s3.py.
 *
 * @param n  Word index within BLK1.
 * @return Raw 32-bit register value.
 */
uint32_t efuseBlk1Word(uint8_t n) { return REG_READ(DR_REG_EFUSE_BASE + 0x44 + 4u * n); }

/** @brief Decoded hardware info, filled once by HwInfo::begin(). */
struct Info {
    bool read = false;
    uint8_t pkg = 0;
    uint8_t flashCap = 0;
    uint8_t flashVendor = 0;
    uint8_t psramCap = 0;
    uint8_t psramVendor = 0;
    uint32_t jedec = 0;
    bool jedecOk = false;
    uint32_t flashReal = 0;
    char module[16] = "?";
} g;

/**
 * @brief Map the eFuse PSRAM capacity code to bytes.
 * @param c  eFuse PSRAM capacity code.
 * @return 8/2/16/4 MiB for codes 1/2/3/4, otherwise 0.
 */
uint32_t psramCapBytes(uint8_t c) {
    switch (c) {
        case 1: return 8u << 20;
        case 2: return 2u << 20;
        case 3: return 16u << 20;
        case 4: return 4u << 20;
        default: return 0;
    }
}

/**
 * @brief Map the eFuse embedded-flash capacity code to bytes.
 * @param c  eFuse flash capacity code.
 * @return 8 MiB for code 1, 4 MiB for code 2, otherwise 0 (no embedded flash / unknown).
 */
uint32_t flashCapBytes(uint8_t c) {
    switch (c) {
        case 1: return 8u << 20;
        case 2: return 4u << 20;
        default: return 0;
    }
}

/**
 * @brief Return the PSRAM bus width for an eFuse PSRAM capacity code.
 *
 * In-package PSRAM of the S3: R2 = quad, R8/R8V/R16V = octal.
 *
 * @param c  eFuse PSRAM capacity code.
 * @return "quad" (code 2), "octal" (codes 1, 3) or "unknown".
 */
const char* psramBus(uint8_t c) {
    switch (c) {
        case 2: return "quad";
        case 1:
        case 3: return "octal";
        default: return "unknown";
    }
}

/**
 * @brief Map the eFuse PSRAM vendor code to the esptool name.
 * @param v  eFuse PSRAM vendor code.
 * @return "AP_3v3", "AP_1v8" or "" if unknown.
 */
const char* psramVendorName(uint8_t v) {
    switch (v) {
        case 1: return "AP_3v3";
        case 2: return "AP_1v8";
        default: return "";
    }
}

/**
 * @brief Map the eFuse embedded-flash vendor code to the esptool name.
 * @param v  eFuse flash vendor code.
 * @return "XMC", "GD", "FM", "TT", "BY" (codes 1..5) or "" if unknown.
 */
const char* flashVendorName(uint8_t v) {
    switch (v) {
        case 1: return "XMC";
        case 2: return "GD";
        case 3: return "FM";
        case 4: return "TT";
        case 5: return "BY";
        default: return "";
    }
}

/**
 * @brief Map a JEDEC manufacturer ID to a vendor name.
 * @param m  JEDEC manufacturer byte (bits 23..16 of the flash ID).
 * @return Vendor name, or "" if unknown.
 */
const char* jedecVendorName(uint8_t m) {
    switch (m) {
        case 0xC8: return "GigaDevice";
        case 0xEF: return "Winbond";
        case 0x20: return "XMC/Micron";
        case 0x68: return "Boya";
        case 0x5E: return "Zbit";
        case 0xC2: return "Macronix";
        case 0x1C: return "EON";
        case 0x0B: return "XTX";
        case 0x85: return "Puya";
        default: return "";
    }
}

/**
 * @brief Map the eFuse package code to a package name.
 * @param p  eFuse package code.
 * @return "QFN56" (0), "PICO-1 LGA56" (1) or "unknown".
 */
const char* pkgName(uint8_t p) {
    switch (p) {
        case 0: return "QFN56";
        case 1: return "PICO-1 LGA56";
        default: return "unknown";
    }
}

}  // namespace

namespace HwInfo {

void begin() {
    if (g.read) return;
    const uint32_t w3 = efuseBlk1Word(3);
    const uint32_t w4 = efuseBlk1Word(4);
    const uint32_t w5 = efuseBlk1Word(5);
    g.pkg = (w3 >> 21) & 0x07;
    g.flashCap = (w3 >> 27) & 0x07;
    g.flashVendor = w4 & 0x07;
    g.psramCap = (uint8_t)(((w5 >> 19) & 0x01) << 2 | ((w4 >> 3) & 0x03));
    g.psramVendor = (w4 >> 7) & 0x03;

    uint32_t id = 0;
    g.jedecOk = esp_flash_read_id(nullptr, &id) == ESP_OK && id != 0 && id != 0xFFFFFF;
    g.jedec = id;
    const uint8_t cap = id & 0xFF;
    if (g.jedecOk && cap >= 0x10 && cap <= 0x1A) {
        g.flashReal = 1u << cap;
    } else {
        g.flashReal = flashCapBytes(g.flashCap);
    }

    const uint32_t psram = psramCapBytes(g.psramCap);
    if (g.flashReal) {
        if (psram) {
            snprintf(g.module, sizeof(g.module), "N%uR%u", (unsigned)(g.flashReal >> 20),
                     (unsigned)(psram >> 20));
        } else {
            snprintf(g.module, sizeof(g.module), "N%u", (unsigned)(g.flashReal >> 20));
        }
    }
    g.read = true;
    Serial.printf("[HW] module=%s flash=%u MiB (jedec 0x%06x) psram=%u MiB %s %s pkg=%s\n",
                  g.module, (unsigned)(g.flashReal >> 20), (unsigned)id,
                  (unsigned)(psram >> 20), psramBus(g.psramCap), psramVendorName(g.psramVendor),
                  pkgName(g.pkg));
}

const char* moduleGuess() { return g.module; }

uint32_t psramBytesEfuse() { return psramCapBytes(g.psramCap); }

uint32_t flashBytesReal() { return g.flashReal; }

void toJson(JsonObject o) {
    char hex[12];
    o["module"] = g.module;
    o["package"] = pkgName(g.pkg);

    JsonObject f = o["flash"].to<JsonObject>();
    f["bytes_real"] = g.flashReal;
    f["bytes_configured"] = ESP.getFlashChipSize();
    f["bytes_unused"] =
        g.flashReal > ESP.getFlashChipSize() ? g.flashReal - ESP.getFlashChipSize() : 0;
    snprintf(hex, sizeof(hex), "0x%06x", (unsigned)g.jedec);
    f["jedec_id"] = g.jedecOk ? hex : "";
    f["vendor"] = g.jedecOk ? jedecVendorName((g.jedec >> 16) & 0xFF) : "";
    f["embedded"] = g.flashCap != 0;
    if (g.flashCap) f["embedded_vendor"] = flashVendorName(g.flashVendor);

    JsonObject p = o["psram"].to<JsonObject>();
    p["bytes_efuse"] = psramCapBytes(g.psramCap);
    p["bus"] = psramBus(g.psramCap);
    p["vendor"] = psramVendorName(g.psramVendor);
    p["bytes_enabled"] = ESP.getPsramSize();
#ifdef BOARD_HAS_PSRAM
    p["build_enabled"] = true;
#else
    p["build_enabled"] = false;
#endif
    const char* memType = g.psramCap == 2 ? "qio_qspi" : (g.psramCap == 1 || g.psramCap == 3)
                                                            ? "qio_opi"
                                                            : "";
    p["memory_type_needed"] = memType;

    JsonObject raw = o["efuse_raw"].to<JsonObject>();
    raw["psram_cap"] = g.psramCap;
    raw["psram_vendor"] = g.psramVendor;
    raw["flash_cap"] = g.flashCap;
    raw["pkg"] = g.pkg;
}

}  // namespace HwInfo
