#pragma once

#include <Arduino.h>
#include <cstring>

/** Lab-only deterministic body fill behind a frozen scan head (Q3b Prefill).
 *
 *  Does not touch StreamBuffer / PSRAM / Detect. Used only when armed via
 *  SoftAP `/api/lab/body_seed` and the MSC read is on a non-live slot.
 *
 *  Layout (16-byte cells, repeating):
 *    [0..3]  "Q3B1"
 *    [4]     tag 'A' or 'B'
 *    [5..7]  0
 *    [8..11] fileOff (big-endian)
 *    [12..15] simple checksum (sum of bytes 0..11, BE uint32)
 */
namespace LabBodySeed {

static constexpr uint32_t kDefaultFromOff = 348160u;  // LBA 761 on fav0 (lba0=81)
static constexpr char kMagic0 = 'Q';
static constexpr char kMagic1 = '3';
static constexpr char kMagic2 = 'B';
static constexpr char kMagic3 = '1';

inline void cellAt(uint8_t* dst16, char tag, uint32_t fileOff) {
    dst16[0] = (uint8_t)kMagic0;
    dst16[1] = (uint8_t)kMagic1;
    dst16[2] = (uint8_t)kMagic2;
    dst16[3] = (uint8_t)kMagic3;
    dst16[4] = (uint8_t)tag;
    dst16[5] = 0;
    dst16[6] = 0;
    dst16[7] = 0;
    dst16[8] = (uint8_t)((fileOff >> 24) & 0xff);
    dst16[9] = (uint8_t)((fileOff >> 16) & 0xff);
    dst16[10] = (uint8_t)((fileOff >> 8) & 0xff);
    dst16[11] = (uint8_t)(fileOff & 0xff);
    uint32_t sum = 0;
    for (int i = 0; i < 12; i++) sum += dst16[i];
    dst16[12] = (uint8_t)((sum >> 24) & 0xff);
    dst16[13] = (uint8_t)((sum >> 16) & 0xff);
    dst16[14] = (uint8_t)((sum >> 8) & 0xff);
    dst16[15] = (uint8_t)(sum & 0xff);
}

inline void fill(uint8_t* out, uint32_t outLen, uint32_t fileOff, char tag) {
    uint32_t o = 0;
    while (o < outLen) {
        const uint32_t abs = fileOff + o;
        const uint32_t into = abs & 15u;
        uint8_t cell[16];
        cellAt(cell, tag, abs - into);
        uint32_t take = 16u - into;
        if (take > outLen - o) take = outLen - o;
        memcpy(out + o, cell + into, take);
        o += take;
    }
}

/** Verify first cell at aligned fileOff; returns false on mismatch. */
inline bool matches(const uint8_t* buf, uint32_t n, char tag, uint32_t fileOff) {
    if (n < 16 || (fileOff & 15u) != 0) return false;
    uint8_t expect[16];
    cellAt(expect, tag, fileOff);
    return memcmp(buf, expect, 16) == 0;
}

}  // namespace LabBodySeed
