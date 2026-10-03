#pragma once

/**
 * Virtual MSC geometry profiles.
 *
 * Default (legacy): FAT12, ~2 MiB, 512 KiB station slots — BMW field baseline 0.4.36.
 * PIDRIVE_GEO_L0:     FAT12, 4 KiB clusters, 4 MiB disk / 1 MiB slots (lab; cluster count <4085).
 * PIDRIVE_GEO_L0_16M: FAT16, 4 KiB clusters, 16 MiB disk / 4 MiB equal slots.
 * PIDRIVE_GEO_L3:     FAT16, 16 MiB disk; fav0=8 MiB measure slot + two 512 KiB companions.
 *
 * Build: env pidrive-s3-l0 | pidrive-s3-l0-16m | pidrive-s3-l3
 */

#include <stdint.h>

namespace MscGeo {

#if defined(PIDRIVE_GEO_L3) && PIDRIVE_GEO_L3

// --- L3: FAT16 16 MiB, unequal slots (8 MiB measure + 2×512 KiB) ---
static constexpr bool kFat16 = true;
static constexpr uint32_t kVirtSectorCount = 32768;  // 16 MiB
static constexpr uint8_t kSpc = 8;
static constexpr uint8_t kFatSpf = 16;
static constexpr uint32_t kFat0Lba = 1;
static constexpr uint32_t kFat1Lba = kFat0Lba + kFatSpf;   // 17
static constexpr uint32_t kRootLba0 = kFat1Lba + kFatSpf;  // 33
static constexpr uint32_t kRootSectors = 32;
static constexpr uint32_t kDataStartLba = kRootLba0 + kRootSectors;  // 65
static constexpr uint32_t kStationsLba = kDataStartLba;
static constexpr uint32_t kSettingsLba = kStationsLba + kSpc;
static constexpr uint32_t kSlotSectors0 = 16384;  // 8 MiB measure (L3)
static constexpr uint32_t kSlotSectors1 = 1024;   // 512 KiB
static constexpr uint32_t kSlotSectors2 = 1024;   // 512 KiB
static constexpr uint32_t kSlotSectors = kSlotSectors0;  // legacy alias
static constexpr const char* kReadyDetail = "FAT16 L3 16MiB fav0=8M";

#elif defined(PIDRIVE_GEO_L0_16M) && PIDRIVE_GEO_L0_16M

// --- L0-16M: true FAT16 (≥4085 clusters) ---
static constexpr bool kFat16 = true;
static constexpr uint32_t kVirtSectorCount = 32768;  // 16 MiB
static constexpr uint8_t kSpc = 8;                   // 4 KiB clusters
static constexpr uint8_t kFatSpf = 16;               // 4096 FAT16 entries
static constexpr uint32_t kFat0Lba = 1;
static constexpr uint32_t kFat1Lba = kFat0Lba + kFatSpf;   // 17
static constexpr uint32_t kRootLba0 = kFat1Lba + kFatSpf;  // 33
static constexpr uint32_t kRootSectors = 32;
static constexpr uint32_t kDataStartLba = kRootLba0 + kRootSectors;  // 65
static constexpr uint32_t kStationsLba = kDataStartLba;              // cl 2
static constexpr uint32_t kSettingsLba = kStationsLba + kSpc;        // cl 3
static constexpr uint32_t kSlotSectors0 = 8192;  // 4 MiB
static constexpr uint32_t kSlotSectors1 = 8192;
static constexpr uint32_t kSlotSectors2 = 8192;
static constexpr uint32_t kSlotSectors = kSlotSectors0;
static constexpr const char* kReadyDetail = "FAT16 L0 16MiB 4M slots";

#elif defined(PIDRIVE_GEO_L0) && PIDRIVE_GEO_L0

// --- L0: 4 MiB disk, 4 KiB clusters, ~1 MiB station slots ---
// IMPORTANT: cluster count ≈ (8192-45)/8 ≈ 1018 < 4085 → hosts classify as FAT12.
static constexpr bool kFat16 = false;
static constexpr uint32_t kVirtSectorCount = 8192;  // 4 MiB
static constexpr uint8_t kSpc = 8;                  // 4 KiB clusters
static constexpr uint8_t kFatSpf = 6;
static constexpr uint32_t kFat0Lba = 1;
static constexpr uint32_t kFat1Lba = kFat0Lba + kFatSpf;  // 7
static constexpr uint32_t kRootLba0 = kFat1Lba + kFatSpf; // 13
static constexpr uint32_t kRootSectors = 32;
static constexpr uint32_t kDataStartLba = kRootLba0 + kRootSectors;  // 45
static constexpr uint32_t kStationsLba = kDataStartLba;
static constexpr uint32_t kSettingsLba = kStationsLba + kSpc;
static constexpr uint32_t kSlotSectors0 = 2048;  // ~1 MiB
static constexpr uint32_t kSlotSectors1 = 2048;
static constexpr uint32_t kSlotSectors2 = 2048;
static constexpr uint32_t kSlotSectors = kSlotSectors0;
static constexpr const char* kReadyDetail = "FAT12 L0 4MiB 1M slots";

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
static constexpr uint32_t kSlotSectors0 = 1024;
static constexpr uint32_t kSlotSectors1 = 1024;
static constexpr uint32_t kSlotSectors2 = 1024;
static constexpr uint32_t kSlotSectors = kSlotSectors0;
static constexpr const char* kReadyDetail = "FAT12 virt 512k slots";

#endif

}  // namespace MscGeo
