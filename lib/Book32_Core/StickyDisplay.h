#pragma once

#if defined(BOARD_SEEED_STICKY)

#include <Adafruit_GFX.h>
#include <bb_epaper.h>
#include <SPI.h>
#include "InkBoyVideo.h"

// GFX-compatible adapter for the Sticky's native-landscape SSD1677 panel.
// InkDeck keeps one 800x480 framebuffer, so existing GFX font and bitmap code
// can run unchanged while bb_epaper handles the panel-specific waveform.
class StickyDisplay : public GFXcanvas1, private InkBoyPanel {
public:
    StickyDisplay();

    void init(uint32_t serialDiagBaud = 115200, bool initial = true,
              uint16_t resetDuration = 10, bool pulldownReset = false);
    void setFullWindow();
    void setPartialWindow(int16_t x, int16_t y, int16_t w, int16_t h);
    void firstPage();
    bool nextPage();
    void refresh(bool partialUpdateMode = false);
    void clearScreen(uint8_t value = 0xFF);
    void hibernate();

    // Inkade fast video: a port of kuslota/InkBoy's display drive
    // (InkBoyDrive.h) on the game window; the rest of the screen is untouched.
    void setGameMode(bool active);
    void setNesGame(bool) {}
    // Borrowed frame, kept alive by the caller until the next setGameFrame or
    // setGameMode(false): dithered, the packed window (InkBoyDrive::packDithered);
    // grayscale, the 2-bit source (InkBoyDrive::packSource).
    void setGameFrame(const uint8_t* frame) { _video.setFrame(frame); }
    // One update (fast_display_update): waits out the drive period, then scans.
    bool refreshGame(bool = false, bool = true) { return _video.refresh(); }
    // Settings, applied the next time game mode starts.
    void setGameSettings(const InkBoyDrive::Settings& settings) { _video.setSettings(settings); }
    // Nonzero while drive remains (pixels, or 1 for the HOLD ending).
    unsigned gamePending() const { return _video.pending(); }
    // Sleep until the next scan may start (fast_display_wait_slot).
    void gameWaitSlot(int64_t imageDeadline) { _video.waitSlot(imageDeadline); }
    // Idle: analog supply off; RAM and waveform are kept.
    void gamePowerOff() { _video.powerOff(); }

    // Grayscale still image (StickyGrayImage): `pulses` holds each pixel's
    // black pulse count, w x h at portrait (x, y). Call right after a full
    // refresh that left that area white. The canvas is left unchanged.
    bool drawGrayImage(const uint8_t* pulses, int x, int y, int w, int h);
    // The same from 8-bit gray (0 black .. 255 white), which is overwritten.
    bool drawGrayCover(uint8_t* gray, int x, int y, int w, int h);

    // Live frames while a gesture moves a page (pinch to zoom, dragging):
    // the canvas, driven with Inkade's one-frame waveform over the whole
    // panel. Begin right after a normal refresh; each liveFrame() starts one
    // scan toward the canvas and returns while the panel drives, so the next
    // frame can be drawn meanwhile. liveEnd() finishes the drive and hands
    // back to normal refreshes.
    bool liveBegin();
    // `frame` is in the canvas layout (800 x 480, 1 = white); null is the canvas.
    bool liveFrame(const uint8_t* frame = nullptr);
    unsigned livePending() const { return _livePending; }
    void liveEnd();
    bool liveActive() const { return _live; }

private:
    BBEPAPER _panel;
    uint8_t* _previousBuffer = nullptr;
    bool _partialWindow = false;
    bool _pagePending = false;
    bool _forceFullRefresh = false;

    // Game video state, allocated on first use and kept for later games.
    bool _gaming = false;
    bool _gamePowered = false;
    uint8_t _gameRotation = 3;
    InkBoyVideo _video;
    // Commands go through an allowlist checked at compile time.
    template <uint8_t Command> void gameCommand(const uint8_t* data = nullptr, size_t length = 0);
    template <uint8_t Command> void gameCommandByte(uint8_t value) { gameCommand<Command>(&value, 1); }
    template <uint8_t Value> void gameUpdate();
    void gameSend(uint8_t command, const uint8_t* data, size_t length);
    void gameWindow();
    bool gameWaitBusy();
    // InkBoyPanel
    bool videoStart(bool gray) override;
    bool videoScan(const uint8_t* oldPlane, const uint8_t* newPlane) override;
    void videoPowerOff() override;

    void flush();

    // Live frames: InkBoy's per-pixel pulse counters at panel size.
    uint32_t* _liveTarget = nullptr;  // What the panel is being driven to
    uint32_t* _liveCount = nullptr;   // 4 bit planes of pulses still to give
    uint8_t* _liveOld = nullptr;
    uint8_t* _liveNew = nullptr;
    bool _live = false, _liveBusy = false;
    unsigned _livePending = 0;
    uint32_t _liveWaitUs = 0, _liveCalcUs = 0, _liveSendUs = 0, _liveScans = 0;  // Timing log
    void liveWindow();
};

#endif
