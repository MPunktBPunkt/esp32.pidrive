#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "core/EventLog.h"
#include "core/MenuStore.h"
#include "core/UartLinkMonitor.h"
#include "core/StreamBuffer.h"
#include "msc/UsbMscGadget.h"

/** PUMP over UART and/or TCP: line-JSON control + binary audio frames.
 *
 * Framing is identical on both transports (V0.4 line-JSON + 0x01 0x55/0x56).
 * TCP default port 9090 — SoftAP (192.168.4.1) or STA IP.
 */
class PumpServer {
public:
    void begin(EventLog* events, MenuStore* menu, UsbMscGadget* msc, UartLinkMonitor* uart,
               StreamBuffer* stream);
    /** Listen for Pi PUMP clients after WiFi/SoftAP is up. port=0 → disabled. */
    void startTcp(uint16_t port);
    void loop();
    bool up() const { return up_; }
    bool tcpListening() const { return tcpPort_ != 0; }
    bool tcpClientUp() const { return tcpLinked_; }
    uint16_t tcpPort() const { return tcpPort_; }
    /** Peer IP of active PUMP-TCP client, or empty. */
    String tcpClientIp();
    void sendPlayUid(const char* uid);
    /** Field diagnostics → Pi bridge log (play.reject / msc.quiet / msc.phase / …). */
    void sendDiag(const char* code, const char* detail = "");
    /** Batched MSC read timeline (drained from UsbMscGadget::loop). */
    void sendMscReads(const MscReadBurst& burst);
    void sendJson(const JsonDocument& doc);
    StreamBuffer* stream() { return stream_; }

    const char* coverSrc() const { return coverSrc_; }
    const char* coverPath() const { return coverPath_; }
    const char* coverTry() const { return coverTry_; }

private:
    enum class Link : uint8_t { SerialLink, Tcp };
    void handleLine(char* line);
    void sendRaw(const char* s);
    void handleBinaryByte(uint8_t c);
    void feedByte(uint8_t c, Link from);
    void drainSerial();
    void drainTcp();
    void acceptTcp();
    void setCoverMeta(const char* src, const char* path, const char* tryList);
    void clearCoverMeta();
    void armOverlayIfWarm();

    EventLog* events_ = nullptr;
    MenuStore* menu_ = nullptr;
    UsbMscGadget* msc_ = nullptr;
    UartLinkMonitor* uart_ = nullptr;
    StreamBuffer* stream_ = nullptr;
    bool up_ = false;
    char line_[1536];
    size_t lineLen_ = 0;
    char coverSrc_[16] = {0};
    char coverPath_[80] = {0};
    char coverTry_[120] = {0};
    Link active_ = Link::SerialLink;
    /** B5: delay msc_->startStream until ring has warmup bytes. */
    char pendingOverlayUid_[24] = {0};
    static constexpr size_t kOverlayWarmupBytes = 8192;

    WiFiServer server_;
    WiFiClient client_;
    uint16_t tcpPort_ = 0;
    char clientIp_[16] = {0};
    bool tcpLinked_ = false;
    uint32_t tcpDownSinceMs_ = 0;
    portMUX_TYPE sendMux_ = portMUX_INITIALIZER_UNLOCKED;

    // Binary: 0x01 0x55 = audio, 0x01 0x56 = sticky ID3 append
    enum class BinState : uint8_t { Idle, GotMagic1, GotMagic2, GotLenLo, Payload };
    BinState binState_ = BinState::Idle;
    uint8_t binKind_ = 0x55;
    uint16_t binLen_ = 0;
    uint16_t binGot_ = 0;
    uint8_t binBuf_[512];
};
