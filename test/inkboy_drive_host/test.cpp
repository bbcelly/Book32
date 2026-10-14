// Host test for the Sticky's port of the InkBoy display drive.
// Build: c++ -std=c++17 -DBOARD_SEEED_STICKY -I lib/Book32_Core test/inkboy_drive_host/test.cpp
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "InkBoyDrive.h"
#include "GameViewport.h"

using namespace InkBoyDrive;

static int popcount(const std::vector<uint8_t>& v) {
    int n = 0;
    for (uint8_t b : v) n += __builtin_popcount(b);
    return n;
}

int main() {
    // --- Dithered counters: a pixel turning black is driven for exactly its
    // budget of scans, then a HOLD ending (all-zero masks) follows.
    {
        static DriveCounters c;
        std::vector<uint8_t> frame(PLANE_BYTES, 0xFF), black(PLANE_BYTES), white(PLANE_BYTES);
        c.seed(frame.data());
        frame[0] = 0x7F;  // first panel pixel black
        int scans = 0;
        while (c.prepare(frame.data(), 8, 8, black.data(), white.data())) {
            assert(black[0] == 0x80 && !white[0] && popcount(black) == 1);
            c.complete(black.data(), white.data());
            ++scans;
            assert(scans <= 8);
        }
        assert(scans == 8 && popcount(black) == 0 && c.pendingPixels == 0);
        // Reversal mid-drive restarts only that pixel's budget, now white.
        frame[0] = 0xFF;
        c.prepare(frame.data(), 8, 3, black.data(), white.data());
        assert(white[0] == 0x80 && !black[0]);
        c.complete(black.data(), white.data());
        int more = 1;
        while (c.prepare(frame.data(), 8, 3, black.data(), white.data())) { c.complete(black.data(), white.data()); ++more; }
        assert(more == 3);
        // Undelivered masks are not consumed.
        frame[1] = 0x00;
        assert(c.prepare(frame.data(), 2, 2, black.data(), white.data()) == 8);
        assert(c.prepare(frame.data(), 2, 2, black.data(), white.data()) == 8);  // nothing completed
    }

    // --- Grayscale planner (InkBoy defaults).
    static std::vector<uint16_t> pixels(PIXELS);
    static std::vector<GrayHistory> history(PIXELS);
    static GrayPlan p;
    std::vector<uint8_t> game(SOURCE_BYTES, 0), black(MASK_BYTES), white(MASK_BYTES), fb(MASK_BYTES), fw(MASK_BYTES);
    p.pixel = pixels.data();
    p.history = history.data();
    p.seed(3);  // white
    p.target(game.data());
    assert(p.pending == 0 && p.masks(black.data(), white.data()) == 0);

    // Single scans: W > K takes dose[W][K] = 7 scans of black drive.
    game[0] = 0xC0;  // first source pixel black (Peanut shade 3)
    p.target(game.data());
    int scans = 0;
    while (p.pending) {
        assert(p.masks(black.data(), white.data()) == 1 && black[0] == 0x80 && !white[0]);
        p.complete(black.data(), white.data());
        ++scans;
        p.target(game.data());
    }
    assert(scans == GRAY_CONFIG_DEFAULT.dose[3][0]);
    assert(GrayPlan::levelOf(pixels[0]) == 0 && GrayPlan::targetOf(pixels[0]) == 0);

    // Pair mode: K > W (HOLD off by default) also drives in the follow-up scan.
    game[0] = 0x00;
    p.target(game.data());
    unsigned pairs = 0;
    while (p.pending) {
        p.masks(black.data(), white.data(), fb.data(), fw.data(), GRAY_HOLDS_DEFAULT);
        assert(white[0] == 0x80);
        p.complete(black.data(), white.data(), fb.data(), fw.data());
        ++pairs;
        p.target(game.data());
    }
    assert(pairs == (GRAY_CONFIG_DEFAULT.dose[0][3] + 1) / 2);
    // W > L has HOLD on by default: no follow-up drive.
    game[0] = 0x40;  // Peanut shade 1 = InkBoy L
    p.target(game.data());
    p.masks(black.data(), white.data(), fb.data(), fw.data(), GRAY_HOLDS_DEFAULT);
    assert(black[0] == 0x80 && !fb[0] && (GRAY_HOLDS_DEFAULT & (1u << (3 * 4 + 2))));

    // Plan order matches GameViewport: from white, a black/white picture's
    // first black masks are GameViewport::pack of that picture.
    std::vector<uint8_t> shades(PIXELS), source(SOURCE_BYTES), packed(GameViewport::PACKED_BYTES);
    std::vector<uint8_t> newPlane(PLANE_BYTES), oldPlane(PLANE_BYTES);
    srand(5);
    for (uint8_t rotation : {uint8_t(1), uint8_t(3)}) {
        for (auto& v : shades) v = rand() & 1 ? 3 : 0;
        packSource(source.data(), shades.data(), rotation);
        p.seed(3);
        p.target(source.data());
        p.masks(black.data(), white.data());
        masksToPlanes(black.data(), white.data(), newPlane.data(), oldPlane.data());
        GameViewport::pack(packed.data(), shades.data(), rotation);
        assert(memcmp(packed.data(), newPlane.data(), PLANE_BYTES) == 0);
        for (uint8_t o : oldPlane) assert(o == 0xFF);  // no white drive
    }

    // Bayer: white has no black dots, black is solid, grays 5/16 and 11/16.
    for (uint8_t shade = 0; shade < 4; ++shade) {
        memset(shades.data(), shade, PIXELS);
        packDithered(packed.data(), shades.data(), 3);
        int blackDots = 0;
        for (uint8_t b : packed) blackDots += 8 - __builtin_popcount(b);
        assert(blackDots == PLANE_BYTES * 8 * BAYER_LEVEL[shade] / 16);
    }

    // Image clock keeps its phase and skips missed slots.
    assert(nextImage(0, 1000) == 1000 + IMAGE_PERIOD_US);
    assert(nextImage(50000, 40000) == 50000);
    assert(nextImage(50000, 50000 + 2 * IMAGE_PERIOD_US + 5) == 50000 + 3 * IMAGE_PERIOD_US);
    assert(wakeTime(0, 10, 100, true, 50) == 10);    // completion fits before the image
    assert(wakeTime(0, 10, 100, true, 95) == 100);   // it does not: wait for the image
    assert(wakeTime(0, 10, 100, false, 50) == 100);

    // Transition test pattern: bands K D L W, lanes of the other shades.
    motionRender(shades.data(), 0);
    for (unsigned bg = 0; bg < 4; ++bg) {
        assert(shades[(bg * 36) * SOURCE_W + 100] == 3 - bg);
        for (unsigned lane = 0; lane < 3; ++lane)
            assert(shades[(bg * 36 + lane * 12 + 2) * SOURCE_W] == 3 - motionPatch(bg, lane));
    }
    assert(motionX(MOTION_LEAD_US + MOTION_TRAVEL_US + 1) == MOTION_MAX_X);

    Settings s = DEFAULT_SETTINGS;
    sanitize(s);
    assert(s.mode == MODE_GRAYSCALE && s.blackPulses == 8 && s.holds == 0xc667 && s.immediate);

    puts("InkBoy drive PASS: dithered counters, gray doses, pair/hold, plan order, Bayer, image clock, test pattern");
    return 0;
}
