#include "NetUtil.h"
#include <WiFi.h>

namespace NetUtil {

String macNoColon() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toUpperCase();
    return mac;
}

String localIp() {
    return WiFi.localIP().toString();
}

String fmtUptime(unsigned long seconds) {
    if (seconds < 60) return String(seconds) + "s";
    if (seconds < 3600) return String(seconds / 60) + "min " + String(seconds % 60) + "s";
    return String(seconds / 3600) + "h " + String((seconds % 3600) / 60) + "min";
}

String chipModel() {
    return String(ESP.getChipModel());
}

void addCors(WebServer& server) {
    server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
    server.sendHeader(F("Access-Control-Allow-Headers"), F("Content-Type"));
}

String jsonToString(const JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    return out;
}

void sendJson(WebServer& server, int code, const JsonDocument& doc) {
    addCors(server);
    server.send(code, F("application/json"), jsonToString(doc));
}

void sendError(WebServer& server, int code, const char* message) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = message;
    sendJson(server, code, doc);
}

bool readJsonBody(WebServer& server, JsonDocument& doc) {
    String body = server.arg("plain");
    if (body.length() == 0) {
        sendError(server, 400, "leerer Body");
        return false;
    }
    if (deserializeJson(doc, body)) {
        sendError(server, 400, "ungueltiges JSON");
        return false;
    }
    return true;
}

}
