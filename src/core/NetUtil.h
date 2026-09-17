#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include "ConfigStore.h"

namespace NetUtil {
String macNoColon();
String localIp();
String fmtUptime(unsigned long seconds);
String chipModel();
void sendJson(WebServer& server, int code, const JsonDocument& doc);
void sendError(WebServer& server, int code, const char* message);
bool readJsonBody(WebServer& server, JsonDocument& doc);
void addCors(WebServer& server);
String jsonToString(const JsonDocument& doc);
}
