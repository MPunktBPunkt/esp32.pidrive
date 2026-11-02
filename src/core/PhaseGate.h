#pragma once

/**
 * @file PhaseGate.h
 * @brief RTC + NVS ENTER/PASS checkpoints (ported/slimmed from hu-probe).
 *
 * If the device resets with an open ENTER (no matching PASS), begin() latches
 * that phase as last_crash for /api/status.
 */

#include <Arduino.h>
#include <ArduinoJson.h>

namespace PhaseGate {

enum Phase : uint8_t {
    None = 0,
    SoftUsb = 1,
    StallBusy = 2,
    OtaFlash = 3,
    MenuApply = 4,
    StreamStart = 5,
    PumpBatch = 6,
};

void begin();
void enter(Phase phase, uint32_t arg = 0);
void pass(Phase phase);
void finish(Phase phase);
const char* name(Phase phase);
void appendStatus(JsonObject obj);
Phase lastCrashPhase();
uint32_t lastCrashArg();

}  // namespace PhaseGate
