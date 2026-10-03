#pragma once

/**
 * Virtual MSC geometry profiles.
 *
 * Default (legacy): FAT12, ~2 MiB, 512 KiB station slots — BMW field baseline 0.4.36.
 * PIDRIVE_GEO_L0:   FAT16, 4 KiB clusters, 4 MiB disk — lab-first for L-ladder (pidrive AUFTRAG-MSC-HOST-READ-NACHWEIS M2).
 *
 * Build: platformio env pidrive-s3-l0  OR  -DPIDRIVE_GEO_L0=1
 */

#include <stdint.h>

namespace MscGeo {

#if defined(PIDRIVE_GEO_L0) && PIDRIVE_GEO_L0

// --- L0: FAT16 / 4 KiB cluster / 4 MiB disk ---
static constexpr bool kFat16 = true;
static constexpr uint32_t kVirtSectorCount = 8192;  // 4 MiB
static constexpr uint8_t kSpc = 8;                  // 4 KiB clusters
static constexpr uint8_t kFatSpf = 4;               // enough for ~1k clusters
static constexpr uint32_t kFat0Lba = 1;
static constexpr uint32_t kFat1Lba = kFat0Lba + kFatSpf;  // 5
static constexpr uint32_t kRootLba0 = kFat1Lba + kFatSpf; // 9
static constexpr uint32_t kRootSectors = 32;              // 512 ents
static constexpr uint32_t kDataStartLba = kRootLba0 + kRootSectors;  // 41
static constexpr uint32_t kStationsLba = kDataStartLba;              // cl 2
static constexpr uint32_t kSettingsLba = kStationsLba + kSpc;        // cl 3
/** ~1 MiB station stubs (3×) — fits 4 MiB; still > legacy 512 KiB. */
static constexpr uint32_t kSlotSectors = 2048;
static constexpr const char* kReadyDetail = "FAT16 L0 4MiB 1M slots";

#else

// --- Legacy: FAT12 / 2 KiB cluster / 2 MiB disk (0.4.21+) ---
static constexpr bool kFat16 = false;
static constexpr uint32_t kVirtSectorCount = 4096;
static constexpr uint8_t kSpc = 4;
static constexpr uint8_t kFatSpf = 8;
static constexpr uint32_t kFat0Lba = 1;
static constexpr uint32_t kFat1Lba = 9;
static constexpr uint32_t kRootLba0 = 17;
static constexpr uint32_t kRootSectors = 32;
static constexpr uint32_t kDataStartLba = 49;
static constexpr uint32_t kStationsLba = 49;
static constexpr uint32_t kSettingsLba = 53;
static constexpr uint32_t kSlotSectors = 1024;
static constexpr const char* kReadyDetail = "FAT12 virt 512k slots";

#endif

}  // namespace MscGeo
