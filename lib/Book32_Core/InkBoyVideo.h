#pragma once
#include <stdint.h>
#include "InkBoyDrive.h"

// The display side of a port of kuslota/InkBoy's fast_display.c: one update
// at a time, with InkBoy's drive periods, HOLD ending and pair hold mode, on
// top of the hardware-free planner in InkBoyDrive.h. A board supplies the
// panel operations; the planes it receives are the packed game window
// (InkBoyDrive::PLANE_BYTES) with, per pixel, old/new = 1/0 black drive,
// 0/1 white drive, equal = hold.
class InkBoyPanel {
public:
    virtual ~InkBoyPanel() = default;
    // Load the one-frame drive waveform (InkBoy: ~20 ms dithered, ~14 ms gray).
    virtual bool videoStart(bool gray) = 0;
    // Send both planes and run one update. False if it was not delivered.
    virtual bool videoScan(const uint8_t* oldPlane, const uint8_t* newPlane) = 0;
    // Analog supply off; RAM and waveform are kept.
    virtual void videoPowerOff() = 0;
};

class InkBoyVideo {
public:
    // Applied on the next start().
    void setSettings(const InkBoyDrive::Settings& settings);
    const InkBoyDrive::Settings& settings() const { return _settings; }
    // Starts on a window that a full refresh just left white.
    bool start(InkBoyPanel* panel);
    void stop();
    bool active() const { return _panel != nullptr; }
    // Borrowed frame: dithered, packDithered output; grayscale, packSource.
    void setFrame(const uint8_t* frame) { _image = frame; }
    // One update (fast_display_update).
    bool refresh();
    // Nonzero while drive remains: pixels, or 1 for the HOLD ending.
    unsigned pending() const;
    // Sleep until the next scan may start (fast_display_wait_slot).
    void waitSlot(int64_t imageDeadline);
    void powerOff() { if (_panel) _panel->videoPowerOff(); }

private:
    InkBoyPanel* _panel = nullptr;
    InkBoyDrive::Settings _settings = InkBoyDrive::DEFAULT_SETTINGS;      // running
    InkBoyDrive::Settings _nextSettings = InkBoyDrive::DEFAULT_SETTINGS;  // next start
    const uint8_t* _image = nullptr;
    uint8_t* _newPlane = nullptr;
    uint8_t* _oldPlane = nullptr;
    uint8_t* _maskBlack = nullptr;    // dithered: panel bits; gray: source bits
    uint8_t* _maskWhite = nullptr;
    uint8_t* _followBlack = nullptr;  // grayscale pair mode follow-up masks
    uint8_t* _followWhite = nullptr;
    InkBoyDrive::DriveCounters* _counters = nullptr;
    InkBoyDrive::GrayPlan* _grayPlan = nullptr;
    uint16_t* _grayPixels = nullptr;
    InkBoyDrive::GrayHistory* _grayHistory = nullptr;
    bool _holdEndPending = false;
    int64_t _lastStart = 0;
    int64_t _scanCost = 40000;
    int64_t _costs[8] = {};
    unsigned _costNext = 0;
    bool gray() const { return _settings.mode == InkBoyDrive::MODE_GRAYSCALE; }
    bool allocate();
    bool scan();
    bool refreshDithered();
    bool refreshGray();
};
