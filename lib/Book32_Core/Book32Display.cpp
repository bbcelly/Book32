#include "Book32Display.h"
#if defined(BOARD_SEEED_XIAO_ESP32S3)
#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>
#include "StickyGrayImage.h"

bool Book32Display::drawGrayCover(uint8_t* gray, int x, int y, int w, int h) {
    int16_t* errors = static_cast<int16_t*>(ps_malloc(sizeof(int16_t) * 2 * (w + 2)));
    if (!errors) return false;
    StickyGrayImage::quantize(gray, w, h, errors, StickyGrayImage::BOOK32_TONES, StickyGrayImage::BOOK32_TONE_COUNT);
    free(errors);
    return drawGrayImage(gray, x, y, w, h);
}

bool Book32Display::drawGrayImage(const uint8_t* pulses, int x, int y, int w, int h) {
    constexpr int W = GxEPD2_750_T7_Fast::WIDTH, H = GxEPD2_750_T7_Fast::HEIGHT, PITCH = W / 8;
    constexpr size_t FRAME_BYTES = size_t(PITCH) * H;
    if (!pulses || _gaming) return false;
    uint8_t maxPulses = 0;
    for (size_t i = 0, n = size_t(w) * h; i < n; ++i) maxPulses = std::max(maxPulses, pulses[i]);
    if (!maxPulses) return true;
    // As on the Sticky: each pass is a native plane (rows of 100 bytes,
    // 1 = white) against an all-white old plane, so every black pixel of the
    // pass gets one black pulse and the rest are held.
    uint8_t* plane = static_cast<uint8_t*>(heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    uint8_t* white = static_cast<uint8_t*>(heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    // Native byte offset and bit of each image pixel (GxEPD2_BW's rotation).
    uint32_t* where = static_cast<uint32_t*>(heap_caps_malloc(sizeof(uint32_t) * w * h, MALLOC_CAP_SPIRAM));
    if (!plane || !white || !where) { free(plane); free(white); free(where); return false; }
    memset(white, 0xFF, FRAME_BYTES);
    const uint8_t rotation = getRotation();
    for (int row = 0; row < h; ++row)
        for (int col = 0; col < w; ++col) {
            int nx = x + col, ny = y + row;
            if (rotation == 1) { const int t = nx; nx = W - 1 - ny; ny = t; }
            else if (rotation == 3) { const int t = nx; nx = ny; ny = H - 1 - t; }
            else if (rotation == 2) { nx = W - 1 - nx; ny = H - 1 - ny; }
            where[row * w + col] = (nx < 0 || nx >= W || ny < 0 || ny >= H)
                ? UINT32_MAX : (uint32_t(ny * PITCH + nx / 8) << 3) | (nx & 7);
        }

    const uint32_t started = millis();
    epd2.gameBegin(GxEPD2_750_T7_Fast::gameFrames ? GxEPD2_750_T7_Fast::gameFrames
                                                  : StickyGrayImage::BOOK32_PULSE_FRAMES);
    for (uint8_t pass = 1; pass <= maxPulses; ++pass) {
        memset(plane, 0xFF, FRAME_BYTES);
        for (size_t i = 0, n = size_t(w) * h; i < n; ++i)
            if (pulses[i] >= pass && where[i] != UINT32_MAX) plane[where[i] >> 3] &= ~(0x80 >> (where[i] & 7));
        epd2.gameScan(white, plane, 0, 0, W, H);
    }
    epd2.gameEnd();
    free(plane);
    free(white);
    free(where);
    Serial.printf("Book32Display: grayscale image %dx%d, %u passes in %lu ms\n", w, h, maxPulses,
                  millis() - started);
    return true;
}
#endif
