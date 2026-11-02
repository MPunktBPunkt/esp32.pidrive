/**
 * @file PhaseGate.cpp
 * @brief RTC + NVS ENTER/PASS gates for pidrive hang localization.
 */

#include "core/PhaseGate.h"

#include <Preferences.h>
#include <cstddef>
#include <esp_attr.h>
#include <esp_system.h>
#include <cstring>

namespace {

constexpr uint32_t kMagic = 0x50444731;  // PDG1
constexpr uint8_t kStackCap = 4;
constexpr const char* kNvsNs = "pdphase";

struct Frame {
    uint8_t phase;
    uint8_t reserved;
    uint16_t reserved2;
    uint32_t arg;
    uint32_t ms;
};

struct Gate {
    uint32_t magic;
    uint32_t generation;
    uint8_t depth;
    uint8_t reserved[3];
    Frame stack[kStackCap];
    uint8_t crashPhase;
    uint8_t crashDepth;
    uint16_t reserved3;
    uint32_t crashArg;
    uint32_t crashMs;
    uint8_t lastPassPhase;
    uint8_t reserved4[3];
    uint32_t lastPassMs;
    uint32_t lastPassArg;
    uint32_t crc;
};

RTC_NOINIT_ATTR Gate retained;

Gate previousCrash;
bool previousCrashValid = false;

uint32_t crc32Bytes(const uint8_t* data, size_t n) {
    uint32_t c = 0xFFFFFFFFUL;
    for (size_t i = 0; i < n; i++) {
        c ^= data[i];
        for (int b = 0; b < 8; b++) {
            c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
        }
    }
    return ~c;
}

uint32_t gateCrc(const Gate& g) {
    return crc32Bytes(reinterpret_cast<const uint8_t*>(&g), offsetof(Gate, crc));
}

bool rtcValid() {
    return retained.magic == kMagic && retained.depth <= kStackCap &&
           retained.crc == gateCrc(retained);
}

void rtcCommit() {
    retained.magic = kMagic;
    retained.crc = gateCrc(retained);
}

bool isPowerLikeReset() {
    const esp_reset_reason_t r = esp_reset_reason();
    return r == ESP_RST_POWERON || r == ESP_RST_BROWNOUT || r == ESP_RST_EXT;
}

bool nvsWriteOpen() {
    Preferences prefs;
    if (!prefs.begin(kNvsNs, false)) return false;
    if (retained.depth == 0) {
        prefs.putUChar("valid", 0);
        prefs.putUChar("sticky", 0);
    } else {
        const Frame& top = retained.stack[retained.depth - 1];
        prefs.putUChar("valid", 1);
        prefs.putUChar("phase", top.phase);
        prefs.putUInt("arg", top.arg);
        prefs.putUInt("ms", top.ms);
        prefs.putUChar("depth", retained.depth);
        prefs.putUChar("sticky", 1);
        prefs.putUChar("s_phase", top.phase);
        prefs.putUInt("s_arg", top.arg);
        prefs.putUInt("s_ms", top.ms);
    }
    prefs.end();
    return true;
}

bool nvsReadCrash(uint8_t& phase, uint32_t& arg, uint32_t& ms) {
    Preferences prefs;
    if (!prefs.begin(kNvsNs, true)) return false;
    uint8_t valid = prefs.getUChar("valid", 0);
    uint8_t sticky = prefs.getUChar("sticky", 0);
    if (valid) {
        phase = prefs.getUChar("phase", 0);
        arg = prefs.getUInt("arg", 0);
        ms = prefs.getUInt("ms", 0);
        prefs.end();
        return phase != 0;
    }
    if (sticky) {
        phase = prefs.getUChar("s_phase", 0);
        arg = prefs.getUInt("s_arg", 0);
        ms = prefs.getUInt("s_ms", 0);
        prefs.end();
        return phase != 0;
    }
    prefs.end();
    return false;
}

void nvsClear() {
    Preferences prefs;
    if (!prefs.begin(kNvsNs, false)) return;
    prefs.putUChar("valid", 0);
    prefs.putUChar("sticky", 0);
    prefs.end();
}

}  // namespace

namespace PhaseGate {

const char* name(Phase phase) {
    switch (phase) {
        case SoftUsb: return "soft_usb";
        case StallBusy: return "stall_busy";
        case OtaFlash: return "ota_flash";
        case MenuApply: return "menu_apply";
        case StreamStart: return "stream_start";
        case PumpBatch: return "pump_batch";
        default: return "none";
    }
}

void begin() {
    previousCrashValid = false;
    memset(&previousCrash, 0, sizeof(previousCrash));

    if (rtcValid() && retained.depth > 0) {
        previousCrash = retained;
        previousCrash.crashPhase = retained.stack[retained.depth - 1].phase;
        previousCrash.crashArg = retained.stack[retained.depth - 1].arg;
        previousCrash.crashMs = retained.stack[retained.depth - 1].ms;
        previousCrash.crashDepth = retained.depth;
        previousCrashValid = true;
    } else if (isPowerLikeReset()) {
        uint8_t phase = 0;
        uint32_t arg = 0, ms = 0;
        if (nvsReadCrash(phase, arg, ms)) {
            previousCrash.crashPhase = phase;
            previousCrash.crashArg = arg;
            previousCrash.crashMs = ms;
            previousCrash.crashDepth = 1;
            previousCrashValid = true;
        }
    }

    memset(&retained, 0, sizeof(retained));
    retained.generation = previousCrashValid ? previousCrash.generation + 1 : 1;
    rtcCommit();
    nvsClear();

    if (previousCrashValid) {
        Serial.printf("[PHASE] last_crash=%s arg=%lu depth=%u\n",
                      name(static_cast<Phase>(previousCrash.crashPhase)),
                      (unsigned long)previousCrash.crashArg,
                      (unsigned)previousCrash.crashDepth);
    }
}

void enter(Phase phase, uint32_t arg) {
    if (phase == None) return;
    if (retained.depth >= kStackCap) {
        Serial.printf("[PHASE] overflow enter %s\n", name(phase));
        return;
    }
    Frame& f = retained.stack[retained.depth++];
    f.phase = static_cast<uint8_t>(phase);
    f.arg = arg;
    f.ms = millis();
    rtcCommit();
    nvsWriteOpen();
}

void pass(Phase phase) {
    if (phase == None || retained.depth == 0) return;
    Frame& top = retained.stack[retained.depth - 1];
    if (top.phase != static_cast<uint8_t>(phase)) {
        Serial.printf("[PHASE] pass mismatch want=%s top=%s\n", name(phase),
                      name(static_cast<Phase>(top.phase)));
        return;
    }
    retained.lastPassPhase = top.phase;
    retained.lastPassArg = top.arg;
    retained.lastPassMs = millis();
    retained.depth--;
    rtcCommit();
    nvsWriteOpen();
}

void finish(Phase phase) {
    while (retained.depth > 0) {
        Frame& top = retained.stack[retained.depth - 1];
        if (top.phase == static_cast<uint8_t>(phase)) {
            pass(phase);
            return;
        }
        pass(static_cast<Phase>(top.phase));
    }
}

void appendStatus(JsonObject obj) {
    JsonObject pg = obj["phase_gate"].to<JsonObject>();
    JsonArray open = pg["open"].to<JsonArray>();
    for (uint8_t i = 0; i < retained.depth; i++) {
        JsonObject f = open.add<JsonObject>();
        f["phase"] = name(static_cast<Phase>(retained.stack[i].phase));
        f["arg"] = retained.stack[i].arg;
        f["ms"] = retained.stack[i].ms;
    }
    if (previousCrashValid && previousCrash.crashPhase) {
        JsonObject crash = pg["last_crash"].to<JsonObject>();
        crash["phase"] = name(static_cast<Phase>(previousCrash.crashPhase));
        crash["arg"] = previousCrash.crashArg;
        crash["ms"] = previousCrash.crashMs;
        crash["depth"] = previousCrash.crashDepth;
    }
    if (retained.lastPassPhase) {
        JsonObject last = pg["last_pass"].to<JsonObject>();
        last["phase"] = name(static_cast<Phase>(retained.lastPassPhase));
        last["arg"] = retained.lastPassArg;
        last["ms"] = retained.lastPassMs;
    }
}

Phase lastCrashPhase() {
    return previousCrashValid ? static_cast<Phase>(previousCrash.crashPhase) : None;
}

uint32_t lastCrashArg() { return previousCrashValid ? previousCrash.crashArg : 0; }

}  // namespace PhaseGate
