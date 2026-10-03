#pragma once

#include <Arduino.h>
#include <cstring>

/**
 * CBR MP3 silence filler for MSC station stubs (Baustelle B2).
 * Tiles a fixed 48 kbit/s MPEG-1 Layer III stereo frame so HU decoders
 * hold the track for the full slot size instead of skipping on 0xFF padding.
 */
namespace Mp3Silence {

/** One MPEG-1 L3 stereo 48 kb/s frame @ 44.1 kHz (pad=0), length 156. */
static constexpr uint16_t kFrameLen = 156;

/** M3 lab markers: distinctive stamp every ~30 s @ 48 kbit/s (≈6000 B/s). */
static constexpr uint32_t kBytesPerSec = 6000;
static constexpr uint32_t kMarkerIntervalBytes = 30u * kBytesPerSec;  // 180000
static constexpr uint8_t kMarkerMagic0 = 'P';
static constexpr uint8_t kMarkerMagic1 = 'D';
static constexpr uint8_t kMarkerMagic2 = 'M';
static constexpr uint8_t kMarkerMagic3 = 'K';

// clang-format off
static const uint8_t kFrame[kFrameLen] = {
    0xff, 0xfb, 0x30, 0x64, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x69, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00,
    0x0d, 0x20, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0xa4, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x34,
    0x80, 0x00, 0x00, 0x04, 0x4c, 0x41, 0x4d, 0x45, 0x33, 0x2e, 0x31, 0x30, 0x30, 0x55, 0x55, 0x55,
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
    0x4c, 0x41, 0x4d, 0x45, 0x33, 0x2e, 0x31, 0x30, 0x30, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
};
// clang-format on

/** Minimal ID3v2.4 + LAME Info frame (208 B audio header @ 64 kb/s) with patchable size. */
static constexpr uint16_t kHeadLen = 252;  // 44 ID3 + 208 Info frame

inline void writeBe32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/** Build ID3 + Info/Xing head for fileSize; frames = payload frames after head. */
inline void writeHead(uint8_t* dest, uint32_t fileSize) {
    // ID3v2.3 header, size=34 (synchsafe) → total ID3 = 44
    static const uint8_t kId3[44] = {
        0x49, 0x44, 0x33, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x54, 0x53, 0x53, 0x45, 0x00, 0x00,
        0x00, 0x0e, 0x00, 0x00, 0x03, 0x4c, 0x61, 0x76, 0x66, 0x36, 0x31, 0x2e, 0x37, 0x2e, 0x31, 0x30,
        0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    memcpy(dest, kId3, 44);

    // First audio frame: reuse structure from lame Info @ 64 kb/s (208 B), patch tag fields.
    memset(dest + 44, 0, 208);
    dest[44] = 0xff;
    dest[45] = 0xfb;
    dest[46] = 0x50;  // 64 kb/s, 44.1 kHz, pad=0
    dest[47] = 0x00;
    // Side info skipped (zeros ok enough for Info-only frame); place "Info" at +36 like LAME.
    const uint32_t tagOff = 44 + 36;
    dest[tagOff + 0] = 'I';
    dest[tagOff + 1] = 'n';
    dest[tagOff + 2] = 'f';
    dest[tagOff + 3] = 'o';
    writeBe32(dest + tagOff + 4, 0x3);  // frames + bytes
    uint32_t payload = fileSize > kHeadLen ? (fileSize - kHeadLen) : 0;
    uint32_t nFrames = payload / kFrameLen;
    writeBe32(dest + tagOff + 8, nFrames);
    writeBe32(dest + tagOff + 12, fileSize);
}

/** Body offset of the first frame that carries marker index `idx` (0-based). */
inline uint32_t markerBodyOff(uint16_t idx) {
    uint32_t raw = (uint32_t)idx * kMarkerIntervalBytes;
    return (raw / kFrameLen) * kFrameLen;
}

/** Absolute file offset of marker `idx` (start of stamped frame), or 0 if past EOF. */
inline uint32_t markerFileOff(uint16_t idx, uint32_t fileSize) {
    if (fileSize <= kHeadLen) return 0;
    uint32_t abs = kHeadLen + markerBodyOff(idx);
    return abs < fileSize ? abs : 0;
}

inline uint16_t markerCount(uint32_t fileSize) {
    if (fileSize <= kHeadLen || kMarkerIntervalBytes == 0) return 0;
    uint32_t body = fileSize - kHeadLen;
    return (uint16_t)(body / kMarkerIntervalBytes + 1);
}

/** Fill read buffer as MP3 silence for absolute file offset (with M3 stamps). */
inline void fill(uint8_t* out, uint32_t outLen, uint32_t fileOff, uint32_t fileSize) {
    uint32_t o = 0;
    while (o < outLen) {
        uint32_t abs = fileOff + o;
        if (abs >= fileSize) {
            memset(out + o, 0, outLen - o);
            break;
        }
        uint32_t n = outLen - o;
        if (abs + n > fileSize) n = fileSize - abs;

        if (abs < kHeadLen) {
            uint8_t head[kHeadLen];
            writeHead(head, fileSize);
            uint32_t take = kHeadLen - abs;
            if (take > n) take = n;
            memcpy(out + o, head + abs, take);
            o += take;
            continue;
        }

        uint32_t body = abs - kHeadLen;
        uint32_t into = body % kFrameLen;
        uint32_t take = kFrameLen - into;
        if (take > n) take = n;
        memcpy(out + o, kFrame + into, take);

        // Stamp first frame of each ~30 s window: keep sync (0..3), write PDMK+idx at 4..9.
        // Note: markerBodyOff(i) aligns down to a frame, so it can be < i*INTERVAL —
        // floor(frameBody0/INTERVAL) alone misses those frames (use guess and guess+1).
        const uint32_t frameBody0 = body - into;
        uint16_t stampIdx = 0;
        bool isMarker = false;
        {
            const uint16_t guess = (uint16_t)(frameBody0 / kMarkerIntervalBytes);
            for (uint16_t d = 0; d < 2; d++) {
                const uint16_t cand = (uint16_t)(guess + d);
                if (markerBodyOff(cand) == frameBody0) {
                    stampIdx = cand;
                    isMarker = true;
                    break;
                }
            }
        }
        if (isMarker) {
            for (uint32_t i = 0; i < take; i++) {
                const uint32_t fr = into + i;  // 0..kFrameLen
                uint8_t v = 0;
                bool stamp = true;
                if (fr == 4) v = kMarkerMagic0;
                else if (fr == 5) v = kMarkerMagic1;
                else if (fr == 6) v = kMarkerMagic2;
                else if (fr == 7) v = kMarkerMagic3;
                else if (fr == 8) v = (uint8_t)(stampIdx >> 8);
                else if (fr == 9) v = (uint8_t)(stampIdx & 0xff);
                else stamp = false;
                if (stamp) out[o + i] = v;
            }
        }
        o += take;
    }
}

}  // namespace Mp3Silence
