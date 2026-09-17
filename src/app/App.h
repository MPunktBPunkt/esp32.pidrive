#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "core/ConfigStore.h"
#include "core/HubClient.h"
#include "core/EventLog.h"
#include "core/MenuStore.h"

class App {
public:
    static App& instance();
    void begin();
    void loop();

    ConfigStore config;
    HubClient hub;
    EventLog events;
    MenuStore menu;

    // Stubs until MSC/PUMP land
    bool usbEnumerated = false;
    bool pumpUp = false;
    uint16_t bufferMs = 0;
    String lastPumpDetail = "-";

private:
    App() = default;
    void setupWifi();
    void setupWeb();
    void buildHeartbeat(JsonDocument& doc);
    void buildStatus(JsonDocument& doc);
    void handleRoot();
    void handleApiStatus();
    void handleApiMenu();
    void handleApiEvents();
    void handleApiEventsClear();
    void handleApiConfigGet();
    void handleApiConfigPost();
    void handleApiLabPlay();
    void handleApiLabUsbToggle();
    void handleOtaUpload();
    void handleRestart();

    WebServer server_{80};
    unsigned long bootMs_ = 0;
};
