#pragma once

#if defined(BOARD_SEEED_XIAO_ESP32S3)

#include <GxEPD2_BW.h>
#include "GxEPD2_750_T7_Fast.h"

// Book32's 7.5" GD7965 panel: GxEPD2 for everything, plus grayscale sleep
// covers pulsed in with the panel's per-pixel drive.
class Book32Display : public GxEPD2_BW<GxEPD2_750_T7_Fast, GxEPD2_750_T7_Fast::HEIGHT> {
public:
    using Base = GxEPD2_BW<GxEPD2_750_T7_Fast, GxEPD2_750_T7_Fast::HEIGHT>;
    explicit Book32Display(GxEPD2_750_T7_Fast driver) : Base(driver) {}

    // As on the Sticky and LILYGO, refresh() shows the drawn buffer (GxEPD2's
    // own refresh only re-shows controller memory).
    void refresh(bool partial = false) { Base::display(partial); }
    using Base::refresh;  // keep the (x, y, w, h) form

    // Grayscale sleep cover: `gray` is 8-bit (0 black .. 255 white), w x h at
    // rotated (x, y), and is overwritten. Call right after a full refresh that
    // left that area white; the image is pulsed in like on the Sticky.
    bool drawGrayCover(uint8_t* gray, int x, int y, int w, int h);
    // `pulses` holds each pixel's black pulse count (StickyGrayImage tones).
    bool drawGrayImage(const uint8_t* pulses, int x, int y, int w, int h);
};

#endif
