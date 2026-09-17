#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "core/EventLog.h"
#include "core/MenuStore.h"
#include "core/UartLinkMonitor.h"
#include "core/StreamBuffer.h"
#include "msc/UsbMscGadget.h"

/** PUMP over UART: line-JSON control + binary audio frames. */
class PumpServer {
public:
    void begin(EventLog* events, MenuStore* menu, UsbMscGadget* msc, UartLinkMonitor* uart,
               StreamBuffer* stream);
    void loop();
    bool up() const { return up_; }
    void sendPlayUid(const char* uid);
    void sendJson(const JsonDocument& doc);
    StreamBuffer* stream() { return stream_; }

private:
    void handleLine(char* line);
    void sendRaw(const char* s);
    void handleBinaryByte(uint8_t c);

    EventLog* events_ = nullptr;
    MenuStore* menu_ = nullptr;
    UsbMscGadget* msc_ = nullptr;
    UartLinkMonitor* uart_ = nullptr;
    StreamBuffer* stream_ = nullptr;
    bool up_ = false;
    char line_[384];
    size_t lineLen_ = 0;

    // Binary frame: 0x01 0x55 | len_lo | len_hi | payload
    enum class BinState : uint8_t { Idle, GotMagic1, GotMagic2, GotLenLo, Payload };
    BinState binState_ = BinState::Idle;
    uint16_t binLen_ = 0;
    uint16_t binGot_ = 0;
    uint8_t binBuf_[512];
};
