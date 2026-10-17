#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Grayscale still images on the Sticky (SSD1677), e.g. sleep covers. The
// picture is built from a white screen with short black pulses: a pixel that
// receives more pulses ends up darker. Each tone is a pulse count and the
// lightness it reaches (0 black .. 255 white), measured with Inkade's
// transition test (5 ms pulses from white: two give light gray, four dark
// gray, ten full black). Gray between tones is error-diffused, so the image
// keeps smooth gradients with far finer steps than black/white dithering.
namespace StickyGrayImage {
struct Tone { uint8_t pulses, lightness; };
constexpr Tone TONES[] = {{0, 255}, {1, 214}, {2, 176}, {3, 134}, {4, 104}, {6, 62}, {10, 0}};
constexpr int TONE_COUNT = sizeof(TONES) / sizeof(TONES[0]);
constexpr uint8_t PULSE_FRAMES = 1;  // 5 ms waveform frames per pulse
constexpr uint8_t MAX_PULSES = TONES[TONE_COUNT - 1].pulses;

// Book32 (GD7965 at PLL 0x0F, ~5 ms frames, BOOK32_PULSE_FRAMES per pulse):
// see Book32Display::drawGrayImage. This panel needs far more drive than the
// Sticky: a deep black takes ~90 frames. Tuned on photographed covers (white
// margin vs dark background), with "b32 ramp" for the light end.
constexpr uint8_t BOOK32_PULSE_FRAMES = 8;
constexpr Tone BOOK32_TONES[] = {{0, 255}, {1, 192}, {2, 136}, {3, 94}, {4, 64},
                                 {5, 43}, {6, 28}, {8, 10}, {11, 0}};
constexpr int BOOK32_TONE_COUNT = sizeof(BOOK32_TONES) / sizeof(BOOK32_TONES[0]);

// LILYGO (FastEPD 4-bit): gray levels 0 (black) .. 15 (white), each driven
// from the cleared white by LilygoDisplay::setGrayPulses with these black
// passes and then white passes (a white pass lifts a gray by ~38). FastEPD's
// stock six-inch matrix put the levels out of order and had nothing between
// black and light gray. Lightness was photographed with "gray ramp", as
// sRGB code relative to the panel's black and white; levels 0-2 all reach
// the panel's black, and 9 and 10 measure the same.
constexpr uint8_t LILYGO_BLACKS[16] = {12, 10, 9, 7, 6, 5, 4, 3, 4, 3, 2, 2, 1, 2, 1, 0};
constexpr uint8_t LILYGO_WHITES[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 2, 1, 0};
constexpr Tone LILYGO_TONES[] = {{0, 0}, {1, 0}, {2, 0}, {3, 31}, {4, 41}, {5, 55}, {6, 79}, {7, 112},
                                 {8, 145}, {9, 168}, {10, 168}, {11, 205}, {12, 227}, {13, 245}, {14, 253}, {15, 255}};
constexpr int LILYGO_TONE_COUNT = sizeof(LILYGO_TONES) / sizeof(LILYGO_TONES[0]);

// Converts 8-bit gray (0 black .. 255 white) to pulse counts in place, with
// serpentine Floyd-Steinberg diffusion of the lightness error. `errors` needs
// 2 * (width + 2) int16 values of scratch.
inline void quantize(uint8_t* pixels, int width, int height, int16_t* errors,
                     const Tone* tones = TONES, int toneCount = TONE_COUNT) {
    int16_t* current = errors;
    int16_t* next = errors + width + 2;
    memset(current, 0, sizeof(int16_t) * (width + 2));
    for (int y = 0; y < height; ++y) {
        memset(next, 0, sizeof(int16_t) * (width + 2));
        const bool reverse = y & 1;
        uint8_t* row = pixels + y * width;
        for (int i = 0; i < width; ++i) {
            const int x = reverse ? width - 1 - i : i;
            int want = row[x] + current[x + 1] / 16;
            want = want < 0 ? 0 : want > 255 ? 255 : want;
            int best = 0;
            for (int t = 1; t < toneCount; ++t)
                if (abs(tones[t].lightness - want) < abs(tones[best].lightness - want)) best = t;
            const int error = want - tones[best].lightness;
            row[x] = tones[best].pulses;
            const int ahead = reverse ? x : x + 2, behind = reverse ? x + 2 : x;
            current[ahead] += error * 7;
            next[behind] += error * 3;
            next[x + 1] += error * 5;
            next[ahead] += error;
        }
        int16_t* swap = current; current = next; next = swap;
    }
}
}
