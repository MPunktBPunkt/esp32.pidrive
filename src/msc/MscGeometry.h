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

// --- L0: 4 MiB disk, 4 KiB clusters, ~1 MiB station slots ---
// IMPORTANT: cluster count ≈ (8192-41)/8 ≈ 1018 < 4085 → hosts classify as FAT12
// regardless of the "FAT16" BPB string. Must use FAT12 encoding on this size.
static constexpr bool kFat16 = false;
static constexpr uint32_t kVirtSectorCount = 8192;  // 4 MiB
static constexpr uint8_t kSpc = 8;                  // 4 KiB clusters
static constexpr uint8_t kFatSpf = 6;               // FAT12: ~1.5 B/cl × ~1020 cl ≈ 4–5 sectors; use 6
static constexpr uint32_t kFat0Lba = 1;
static constexpr uint32_t kFat1Lba = kFat0Lba + kFatSpf;  // 7
static constexpr uint32_t kRootLba0 = kFat1Lba + kFatSpf; // 13
static constexpr uint32_t kRootSectors = 32;
static constexpr uint32_t kDataStartLba = kRootLba0 + kRootSectors;  // 45
static constexpr uint32_t kStationsLba = kDataStartLba;              // cl 2
static constexpr uint32_t kSettingsLba = kStationsLba + kSpc;        // cl 3
static constexpr uint32_t kSlotSectors = 2048;  // ~1 MiB
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
static constexpr uint32_t kSlotSectors = 1024;
static constexpr const char* kReadyDetail = "FAT12 virt 512k slots";

#endif

}  // namespace MscGeo
