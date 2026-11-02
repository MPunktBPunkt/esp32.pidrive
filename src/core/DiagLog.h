#pragma once

/**
 * @file DiagLog.h
 * @brief Diagnostic text log: Serial + optional PSRAM ring + RTC tail (slim pidrive port).
 *
 * USB bulk paths must NOT call persist-like flash writes. This slim port has no
 * flash snapshots — only Serial, a PSRAM ring (readable via /api/diag/log), and a
 * 2 KiB RTC ring that survives soft-reset/panic/WDT.
 */

#include <Arduino.h>
#include <ArduinoJson.h>

namespace DiagLog {

void begin();
void printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
const char* previousTail();
size_t size();
size_t copyOut(size_t offset, char* dst, size_t n);
void appendStatus(JsonObject obj);
void appendPreviousTail(JsonObject obj);

}  // namespace DiagLog
