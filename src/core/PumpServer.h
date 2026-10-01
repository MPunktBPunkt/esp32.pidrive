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
    void emitPlayUid_(const char* uid);
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
    /** Pending uid until overlay armed (B6: armed immediately when warmup==0). */
    char pendingOverlayUid_[24] = {0};
    /** B5→B6: arm MSC overlay immediately on audio_start (was 8192).
     * Feld 2026-10-01: HU consumed ~182 KiB B2 silence before warm → streamBytes=0.
     * Warmup=0 closes the pre-warm silence window; measure preWarmHostBytes≈0 + streamBytes. */
    static constexpr size_t kOverlayWarmupBytes = 0;

    WiFiServer server_;
    WiFiClient client_;
    uint16_t tcpPort_ = 0;
    char clientIp_[16] = {0};
    bool tcpLinked_ = false;
    uint32_t tcpDownSinceMs_ = 0;
    /** Serialize line sends (USB diag/play vs loop msc.reads). Never spinlock — TCP needs IRQs. */
    SemaphoreHandle_t sendMu_ = nullptr;
    /** Last play.guess uid — re-emit on hello if bridge missed it (TCP was down). */
    char queuedPlayUid_[24] = {0};

    // Binary: 0x01 0x55 = audio, 0x01 0x56 = sticky ID3 append
    enum class BinState : uint8_t { Idle, GotMagic1, GotMagic2, GotLenLo, Payload };
    BinState binState_ = BinState::Idle;
    uint8_t binKind_ = 0x55;
    uint16_t binLen_ = 0;
    uint16_t binGot_ = 0;
    uint8_t binBuf_[512];
};
