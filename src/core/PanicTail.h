#pragma once

/**
 * @file PanicTail.h
 * @brief RTC-retained lifecycle breadcrumbs for post-reset diagnosis (ported from hu-probe).
 */

#include <Arduino.h>
#include <ArduinoJson.h>

/**
 * Small RTC slow-memory tail. Writes never touch flash. Holds up to 32 breadcrumbs.
 * Call begin() once early in App::begin(), before any breadcrumb().
 */
class PanicTail {
public:
    enum Code : uint8_t {
        SessionStart = 1,
        SoftDetach = 5,
        UsbMount = 7,
        UsbUmount = 8,
        BotReset = 9,
        NeedReset = 10,
        EpStall = 11,
        MediaSwitch = 12,
        WifiLost = 15,
        RestartRequested = 19,
        StallEnter = 20,
        StallExit = 21,
        PumpClient = 22,
        OtaBegin = 23,
        StreamBegin = 24,
    };

    enum RestartCause : uint8_t {
        RestartNone = 0,
        RestartWifiUnrecoverable = 1,
        RestartPortalTimeout = 2,
        RestartHubOta = 3,
        RestartWebUi = 4,
        RestartGithubOta = 5,
        RestartOtaRollback = 6,
    };

    static void begin();
    static void setSession(const char* sid);
    static void breadcrumb(Code code, uint32_t arg = 0);
    /** Record cause then return; caller still calls ESP.restart(). */
    static void noteRestart(RestartCause cause);

    static void appendPrevious(JsonObject obj);
    static void appendCurrent(JsonObject obj);
    static void appendBootInfo(JsonObject obj);

    static const char* resetReason();
    static const char* restartCause();
    static uint32_t bootGeneration();

private:
    PanicTail() = delete;
};
