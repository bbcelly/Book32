#pragma once

#if defined(BOARD_SEEED_XIAO_ESP32S3)

#include <GxEPD2_BW.h>
#include "GxEPD2_750_T7_Fast.h"
#include "GameViewport.h"
#include "InkBoyVideo.h"

// Book32's 7.5" GD7965 panel: GxEPD2 for everything, plus Inkade's fast game
// video (the port of InkBoy's drive) on the 320x288 game window. Same game
// interface as StickyDisplay.
class Book32Display : public GxEPD2_BW<GxEPD2_750_T7_Fast, GxEPD2_750_T7_Fast::HEIGHT>, private InkBoyPanel {
public:
    using Base = GxEPD2_BW<GxEPD2_750_T7_Fast, GxEPD2_750_T7_Fast::HEIGHT>;
    explicit Book32Display(GxEPD2_750_T7_Fast driver) : Base(driver) {}

    // As on the Sticky and LILYGO, refresh() shows the drawn buffer (GxEPD2's
    // own refresh only re-shows controller memory). Inkade draws, then refreshes.
    void refresh(bool partial = false) { Base::display(partial); }
    using Base::refresh;  // keep the (x, y, w, h) form

    void setGameMode(bool active) {
        if (active == _gaming) return;
        if (active) {
            _rotation = getRotation() == 1 ? 1 : 3;
            _gaming = _video.start(this);
        } else {
            _video.stop();
            epd2.gameEnd();
            _gaming = false;
        }
    }
    void setNesGame(bool) {}
    void setGameFrame(const uint8_t* frame) { _video.setFrame(frame); }
    bool refreshGame(bool = false, bool = true) { return _video.refresh(); }
    void setGameSettings(const InkBoyDrive::Settings& settings) { _video.setSettings(settings); }
    unsigned gamePending() const { return _video.pending(); }
    void gameWaitSlot(int64_t imageDeadline) { _video.waitSlot(imageDeadline); }
    void gamePowerOff() { _video.powerOff(); }
    bool gameActive() const { return _gaming; }

    // Grayscale sleep cover: `gray` is 8-bit (0 black .. 255 white), w x h at
    // rotated (x, y), and is overwritten. Call right after a full refresh that
    // left that area white; the image is pulsed in like on the Sticky.
    bool drawGrayCover(uint8_t* gray, int x, int y, int w, int h);
    // `pulses` holds each pixel's black pulse count (StickyGrayImage tones).
    bool drawGrayImage(const uint8_t* pulses, int x, int y, int w, int h);

private:
    // GD7965 at PLL 0x0F (gameBegin): ~5.2 ms frames, plus ~40 ms of fixed
    // refresh overhead (measured: 45 ms busy for 1 frame, 61 ms for 4). The
    // default 50 Hz rate costs ~116 ms per refresh. Pulses as InkBoy's:
    // ~20 ms dithered, ~15 ms grayscale.
    // This panel moves much less per frame than the Sticky: 4-frame (21 ms)
    // pulses left even static text pale gray; 10 frames (52 ms) gave solid
    // black (photographed). Grayscale scaled to match; not tuned yet.
    static constexpr uint8_t DITHER_FRAMES = 10, GRAY_FRAMES = 7;
    InkBoyVideo _video;
    bool _gaming = false;
    uint8_t _rotation = 3;

    bool videoStart(bool gray) override {
        epd2.gameBegin(GxEPD2_750_T7_Fast::gameFrames ? GxEPD2_750_T7_Fast::gameFrames
                                                      : gray ? GRAY_FRAMES : DITHER_FRAMES);
        return true;
    }
    bool videoScan(const uint8_t* oldPlane, const uint8_t* newPlane) override {
        // The packed window's rows run along native y from nativeY0, each row
        // GameViewport::H native pixels from nativeX.
        epd2.gameScan(oldPlane, newPlane, GameViewport::nativeX(_rotation), GameViewport::nativeY0(_rotation),
                      GameViewport::H, GameViewport::WINDOW_ROWS);
        return true;
    }
    void videoPowerOff() override { epd2.gamePowerOff(); }
};

#endif
