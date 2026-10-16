#!/usr/bin/env python3
"""Source-level safety contracts for esp-capture-port (no hardware required)."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    config_h = (ROOT / "src/core/ConfigStore.h").read_text(encoding="utf-8")
    capture_h = (ROOT / "src/msc/MessCapture.h").read_text(encoding="utf-8")
    capture_cpp = (ROOT / "src/msc/MessCapture.cpp").read_text(encoding="utf-8")
    msc_cpp = (ROOT / "src/msc/UsbMscGadget.cpp").read_text(encoding="utf-8")
    app_cpp = (ROOT / "src/app/App.cpp").read_text(encoding="utf-8")
    pump_cpp = (ROOT / "src/core/PumpServer.cpp").read_text(encoding="utf-8")
    platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")

    assert "bool selectionBarrierEnable = false;" in config_h
    assert "using ProbeClock = int64_t;" in capture_h
    assert 'obj["port"] = "esp-capture-port";' in capture_cpp
    assert "copyAfter(uint64_t afterSeq" in capture_h
    assert "static constexpr size_t kRing = 512;" in capture_h
    assert "RamLocal" in capture_h and "RamWifiOff" in capture_h
    assert '"ram_local"' in capture_cpp and '"ram_wifi_off"' in capture_cpp
    for health_key in (
        "startSeq",
        "endSeq",
        "overflow",
        "drop",
        "complete",
        "mode",
        "endReason",
    ):
        assert f'obj["{health_key}"]' in capture_cpp, health_key
    assert "cur_.source = MessSource::Synthesized;" in capture_cpp
    assert '\\"source\\":\\"%s\\"' in pump_cpp
    assert '"synthesized"' in pump_cpp
    assert "telemetrySuppressed" in app_cpp
    assert "if (!mess.captureActive())" in app_cpp
    assert app_cpp.count("MessCapture::instance().captureActive()") >= 2
    mess_handler = app_cpp.split("void App::handleApiLabMess()", 1)[1].split(
        "void App::handleApiLabSituation()", 1
    )[0]
    assert mess_handler.index('body["reset"]') < mess_handler.index("mess.beginCapture")
    assert 'obj["complete"] = captureComplete && captureOverflow == 0;' in capture_cpp
    assert '-DFW_VERSION=\\"0.4.52-dev\\"' in platformio

    callback = capture_cpp.split(
        'extern "C" int32_t __wrap_tud_msc_read10_cb', 1
    )[1].split('extern "C" int32_t __wrap_tud_msc_write10_cb', 1)[0]
    for forbidden in ("Json", "WiFi", "String", "malloc", "new ", "send"):
        assert forbidden not in callback, forbidden

    pre_payload = msc_cpp.split(
        "// Base: demo image for low LBAs", 1
    )[0].rsplit("int32_t UsbMscGadget::onRead", 1)[1]
    assert "selectionBarrierRead" in pre_payload
    barrier_cb = msc_cpp.split(
        "int32_t UsbMscGadget::selectionBarrierRead", 1
    )[1].split("void UsbMscGadget::killSelectionBarrier", 1)[0]
    for forbidden in (
        "playHandler_",
        "events_->",
        "menu_->",
        "setPhase(",
        "Json",
        "WiFi",
        "String",
    ):
        assert forbidden not in barrier_cb, forbidden

    # audio_start's internal stop must preserve only the matching barrier UID.
    assert "msc_->stopStream(uid);" in pump_cpp
    stop_stream = msc_cpp.split("void UsbMscGadget::stopStream(const char* nextUid)", 1)[
        1
    ].split("void UsbMscGadget::patchFatChain", 1)[0]
    assert "strcmp(nextUid, selectionBarrierUid_) == 0" in stop_stream
    assert "if (!preserveBarrier) killSelectionBarrier();" in stop_stream

    # Ready is a successful release, not a kill, and arms scope-1 pacing state.
    ready_block = barrier_cb.split("if (ready)", 1)[1].split(
        "if (waited >=", 1
    )[0]
    assert "releaseSelectionBarrier();" in ready_block
    assert "killSelectionBarrier();" not in ready_block
    release = barrier_cb.split("void UsbMscGadget::releaseSelectionBarrier()", 1)[1]
    assert "stallArmed_ = true;" in release
    assert "stallLeadLeft_ = stall_.leadBytes;" in release
    assert "selectionBarrierReadyPending_ = true;" in release
    assert "selectionBarrierKillN_++" not in release
    assert "silenceBytesServed_[si] += bufsize;" in msc_cpp
    assert 'obj["silenceBytes"].to<JsonArray>()' in msc_cpp
    loop = msc_cpp.split("void UsbMscGadget::loop()", 1)[1].split(
        "uint32_t UsbMscGadget::msSincePlug", 1
    )[0]
    assert "if (selectionBarrierReadyPending_)" in loop
    assert 'events_->push("selection_barrier.ready"' in loop
    assert "setPhase(Phase::Play" in loop

    print("PASS test_capture_contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
