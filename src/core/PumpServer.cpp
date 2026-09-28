#include "PumpServer.h"
#include "BuildFlags.h"
#include <ArduinoJson.h>
#include <cstring>

void PumpServer::begin(EventLog* events, MenuStore* menu, UsbMscGadget* msc, UartLinkMonitor* uart,
                       StreamBuffer* stream) {
    events_ = events;
    menu_ = menu;
    msc_ = msc;
    uart_ = uart;
    stream_ = stream;
    lineLen_ = 0;
    up_ = false;
    binState_ = BinState::Idle;
    active_ = Link::SerialLink;
    tcpPort_ = 0;
    if (events_) events_->push("pump.init", "line-json+bin uart/tcp");
}

void PumpServer::startTcp(uint16_t port) {
    if (port == 0) {
        tcpPort_ = 0;
        if (client_.connected()) client_.stop();
        clientIp_[0] = 0;
        tcpLinked_ = false;
        Serial.println("[PUMP] TCP off");
        return;
    }
    tcpPort_ = port;
    server_.begin(port);
    server_.setNoDelay(true);
    Serial.printf("[PUMP] TCP :%u (same framing as UART)\n", (unsigned)port);
    if (events_) {
        char det[24];
        snprintf(det, sizeof(det), ":%u", (unsigned)port);
        events_->push("pump.tcp", det);
    }
}

String PumpServer::tcpClientIp() {
    // Do not trust WiFiClient.connected() here — it clears the peer IP in the UI.
    if (!tcpLinked_) {
        return String();
    }
    if (!clientIp_[0]) {
        String ip = client_.remoteIP().toString();
        strncpy(clientIp_, ip.c_str(), sizeof(clientIp_) - 1);
        clientIp_[sizeof(clientIp_) - 1] = 0;
    }
    return String(clientIp_);
}

void PumpServer::sendRaw(const char* s) {
    if (!s) return;
    // Always prefer the sticky TCP peer when linked. Using active_==Tcp alone
    // dropped menu_ack/audio_ack onto Serial after SoftAP nav (menu looked frozen).
    if (tcpLinked_) {
        size_t n = client_.println(s);
        client_.flush();
        if (n > 0) tcpDownSinceMs_ = 0;
    } else {
        Serial.println(s);
    }
}

void PumpServer::sendJson(const JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    sendRaw(out.c_str());
}

void PumpServer::sendPlayUid(const char* uid) {
    if (!uid || !uid[0]) return;
    // Prefer live TCP bridge (field), else UART
    if (tcpLinked_) active_ = Link::Tcp;
    else active_ = Link::SerialLink;
    JsonDocument doc;
    doc["t"] = "event";
    doc["op"] = "play_uid";
    doc["uid"] = uid;
    sendJson(doc);
    if (events_) events_->push("pump.event", uid);
}

void PumpServer::sendDiag(const char* code, const char* detail) {
    if (!code || !code[0]) return;
    // Only when a PUMP peer is actually linked (avoid Serial spam without bridge)
    if (!tcpLinked_ && !up_) return;
    if (tcpLinked_) active_ = Link::Tcp;
    else active_ = Link::SerialLink;
    JsonDocument doc;
    doc["t"] = "event";
    doc["op"] = "diag";
    doc["code"] = code;
    if (detail && detail[0]) doc["detail"] = detail;
    sendJson(doc);
}

void PumpServer::setCoverMeta(const char* src, const char* path, const char* tryList) {
    coverSrc_[0] = coverPath_[0] = coverTry_[0] = 0;
    if (src && src[0]) {
        strncpy(coverSrc_, src, sizeof(coverSrc_) - 1);
        coverSrc_[sizeof(coverSrc_) - 1] = 0;
    }
    if (path && path[0]) {
        strncpy(coverPath_, path, sizeof(coverPath_) - 1);
        coverPath_[sizeof(coverPath_) - 1] = 0;
    }
    if (tryList && tryList[0]) {
        strncpy(coverTry_, tryList, sizeof(coverTry_) - 1);
        coverTry_[sizeof(coverTry_) - 1] = 0;
    }
}

void PumpServer::clearCoverMeta() {
    coverSrc_[0] = coverPath_[0] = coverTry_[0] = 0;
}

void PumpServer::handleBinaryByte(uint8_t c) {
    switch (binState_) {
        case BinState::Idle:
            if (c == 0x01) binState_ = BinState::GotMagic1;
            break;
        case BinState::GotMagic1:
            if (c == 0x55 || c == 0x56) {
                binKind_ = c;
                binState_ = BinState::GotMagic2;
            } else if (c == 0x01) {
                binState_ = BinState::GotMagic1;
            } else {
                binState_ = BinState::Idle;
            }
            break;
        case BinState::GotMagic2:
            binLen_ = c;
            binState_ = BinState::GotLenLo;
            break;
        case BinState::GotLenLo:
            binLen_ |= (uint16_t)c << 8;
            binGot_ = 0;
            if (binLen_ == 0 || binLen_ > sizeof(binBuf_)) {
                binState_ = BinState::Idle;
            } else {
                binState_ = BinState::Payload;
            }
            break;
        case BinState::Payload:
            binBuf_[binGot_++] = c;
            if (binGot_ >= binLen_) {
                if (stream_ && stream_->active()) {
                    if (binKind_ == 0x56) {
                        stream_->appendId3(binBuf_, binLen_);
                    } else {
                        stream_->push(binBuf_, binLen_);
                    }
                }
                binState_ = BinState::Idle;
            }
            break;
    }
}

void PumpServer::feedByte(uint8_t c, Link from) {
    active_ = from;
    if (binState_ != BinState::Idle) {
        handleBinaryByte(c);
        return;
    }
    if (c == 0x01) {
        handleBinaryByte(c);
        return;
    }
    if (c == '\n' || c == '\r') {
        if (lineLen_ > 0) {
            line_[lineLen_] = 0;
            handleLine(line_);
            lineLen_ = 0;
        }
        return;
    }
    if (lineLen_ + 1 < sizeof(line_)) {
        line_[lineLen_++] = (char)c;
    } else {
        lineLen_ = 0;
    }
}

void PumpServer::handleLine(char* line) {
    while (*line == ' ' || *line == '\t') line++;
    if (!line[0]) return;

    if (!strncmp(line, "PING", 4) || !strncmp(line, "DBG", 3)) {
        sendRaw("PONG");
        return;
    }

    if (line[0] != '{') return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        if (events_) events_->push("pump.badjson", err.c_str());
        return;
    }

    const char* t = doc["t"] | "";
    if (!strcmp(t, "hello")) {
        up_ = true;
        JsonDocument ack;
        ack["t"] = "hello_ack";
        ack["ver"] = FW_VERSION;
        ack["fw"] = FW_TYPE;
        ack["slots"] = 4;
        ack["page"] = true;  // bridge soft-paging (Mehr… / Seite 1)
        ack["audio"] = true;
        ack["bin"] = true;
        ack["tcp"] = tcpPort_ != 0;
        ack["tcpPort"] = (int)tcpPort_;
        sendJson(ack);
        if (events_) events_->push("pump.hello", active_ == Link::Tcp ? "tcp" : "uart");
        return;
    }

    if (!strcmp(t, "menu_set")) {
        binState_ = BinState::Idle;  // don't eat JSON if a prior bin frame was truncated
        JsonArrayConst items = doc["items"].as<JsonArrayConst>();
        uint32_t rev = doc["rev"] | 0;
        if (!menu_ || items.isNull()) {
            JsonDocument nak;
            nak["t"] = "menu_ack";
            nak["ok"] = false;
            sendJson(nak);
            return;
        }
        menu_->setFromJson(items, rev);
        if (msc_) {
            msc_->applyMenuSlots(*menu_);
            msc_->presentMedia("menu_set");
        }
        up_ = true;
        JsonDocument ack;
        ack["t"] = "menu_ack";
        ack["ok"] = true;
        ack["n"] = (int)menu_->count();
        ack["rev"] = rev;
        sendJson(ack);
        char det[40];
        snprintf(det, sizeof(det), "n=%u rev=%lu", (unsigned)menu_->count(), (unsigned long)rev);
        if (events_) events_->push("menu.set", det);
        return;
    }

    if (!strcmp(t, "audio_start")) {
        const char* uid = doc["uid"] | "";
        const char* cSrc = doc["cSrc"] | "";
        const char* cPath = doc["cPath"] | "";
        const char* cTry = doc["cTry"] | "";
        setCoverMeta(cSrc, cPath, cTry);
        if (stream_) {
            stream_->start(uid);
            stream_->clearId3();
        }
        if (msc_) msc_->startStream(uid);
        up_ = true;
        JsonDocument ack;
        ack["t"] = "audio_ack";
        ack["ok"] = true;
        ack["op"] = "start";
        ack["uid"] = uid;
        ack["id3"] = true;
        sendJson(ack);
        if (events_) events_->push("audio.start", uid);
        return;
    }

    if (!strcmp(t, "audio_stop")) {
        if (stream_) stream_->stop();
        if (msc_) msc_->stopStream();
        clearCoverMeta();
        JsonDocument ack;
        ack["t"] = "audio_ack";
        ack["ok"] = true;
        ack["op"] = "stop";
        sendJson(ack);
        if (events_) events_->push("audio.stop", "");
        return;
    }

    if (!strcmp(t, "ping")) {
        JsonDocument pong;
        pong["t"] = "pong";
        sendJson(pong);
    }
}

void PumpServer::acceptTcp() {
    if (tcpPort_ == 0) return;
    WiFiClient incoming = server_.available();
    if (!incoming) return;

    // Sticky link: keep a healthy client. Replace only when the old one has been
    // !connected() for a bit — instant replace on WiFiClient flaps killed the
    // live Pi session and froze SoftAP menu sync in a reconnect loop.
    if (tcpLinked_) {
        if (client_ && client_.connected()) {
            incoming.stop();
            return;
        }
        uint32_t now = millis();
        if (tcpDownSinceMs_ == 0) tcpDownSinceMs_ = now;
        if (now - tcpDownSinceMs_ < 2000) {
            incoming.stop();
            return;
        }
        if (client_) client_.stop();
        tcpLinked_ = false;
        if (events_) events_->push("pump.tcp.replace", clientIp_);
    }

    client_ = incoming;
    client_.setNoDelay(true);
    binState_ = BinState::Idle;
    lineLen_ = 0;
    active_ = Link::Tcp;
    tcpDownSinceMs_ = 0;
    {
        String ip = client_.remoteIP().toString();
        strncpy(clientIp_, ip.c_str(), sizeof(clientIp_) - 1);
        clientIp_[sizeof(clientIp_) - 1] = 0;
    }
    Serial.printf("[PUMP] TCP client %s\n", clientIp_);
    tcpLinked_ = true;
    if (events_) events_->push("pump.tcp.up", clientIp_);
}

void PumpServer::drainTcp() {
    if (!tcpLinked_) return;

    while (client_.available() > 0) {
        int c = client_.read();
        if (c < 0) break;
        feedByte((uint8_t)c, Link::Tcp);
        tcpDownSinceMs_ = 0;
    }

    // WiFiClient.connected() flaps on ESP32 — only drop after sustained idle+down.
    if (client_.connected()) {
        tcpDownSinceMs_ = 0;
        return;
    }

    uint32_t now = millis();
    if (tcpDownSinceMs_ == 0) tcpDownSinceMs_ = now;
    // 20s debounce: SoftAP+STA + binary ID3/MP3 often reports !connected()
    // for several seconds while the socket is still alive.
    if (now - tcpDownSinceMs_ < 20000) return;

    client_.stop();
    clientIp_[0] = 0;
    tcpLinked_ = false;
    tcpDownSinceMs_ = 0;
    Serial.println("[PUMP] TCP client down");
    if (events_) events_->push("pump.tcp.down", "");
    if (active_ == Link::Tcp) {
        up_ = false;
        active_ = Link::SerialLink;
    }
}

void PumpServer::drainSerial() {
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c < 0) break;
        if (uart_) uart_->noteRx(1);
        feedByte((uint8_t)c, Link::SerialLink);
    }
}

void PumpServer::loop() {
    acceptTcp();
    // Prefer draining the active link first to keep binary frames contiguous
    if (active_ == Link::Tcp) {
        drainTcp();
        drainSerial();
    } else {
        drainSerial();
        drainTcp();
    }
}
