#pragma once
#include <stdint.h>
#include <string.h>

// Port of the display drive from kuslota/InkBoy (MIT, copyright kuslota):
// main/drive_counters.c (fast dithered) and main/gray_plan.c (fast
// grayscale), with InkBoy's default settings. The algorithms and parameters
// are unchanged; only the geometry follows the Sticky, where the 160x144
// Game Boy image is shown at 2x in a 320x288 window of an SSD1677 panel.
// Hardware-free, so host tests can exercise it.
namespace InkBoyDrive {

// --- Geometry ---------------------------------------------------------------
// The packed window is the controller's RAM layout (GameViewport): 320 write
// rows of 36 bytes, 1 = white. Write rows 2k and 2k + 1 show source column k
// in "plan order"; byte n of a row holds four source pixels (two native
// columns each), first pixel in the high bits.
constexpr int SOURCE_W = 160, SOURCE_H = 144, PIXELS = SOURCE_W * SOURCE_H;
constexpr int ROW_BYTES = SOURCE_H * 2 / 8, WINDOW_ROWS = SOURCE_W * 2;
constexpr int PLANE_BYTES = ROW_BYTES * WINDOW_ROWS;
constexpr int SOURCE_BYTES = PIXELS / 4;  // 2 bits per source pixel, plan order
constexpr int MASK_BYTES = PIXELS / 8;    // 1 bit per source pixel, plan order
constexpr int WIN_W = SOURCE_W * 2, WIN_H = SOURCE_H * 2;

inline int planX(int k, uint8_t rotation) { return rotation == 3 ? SOURCE_W - 1 - k : k; }
inline int planY(int j, uint8_t rotation) { return rotation == 3 ? j : SOURCE_H - 1 - j; }

// 160x144 Peanut-GB shades (0 white .. 3 black) -> 2-bit plan order.
inline void packSource(uint8_t* out, const uint8_t* shades, uint8_t rotation) {
    for (int k = 0; k < SOURCE_W; ++k) {
        const uint8_t* column = shades + planX(k, rotation);
        uint8_t* o = out + k * (SOURCE_H / 4);
        for (int j = 0; j < SOURCE_H; j += 4) {
            uint8_t v = 0;
            for (int i = 0; i < 4; ++i) v = (v << 2) | (column[planY(j + i, rotation) * SOURCE_W] & 3);
            *o++ = v;
        }
    }
}

// --- Fast dithered: fixed 4x4 Bayer pattern ---------------------------------
// InkBoy converts the four shades with a fixed 4x4 Bayer matrix tied to screen
// positions: white has no black dots, the grays about 5/16 and 11/16 black,
// black is solid. Here it is applied to the 2x panel pixels.
constexpr uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
constexpr uint8_t BAYER_LEVEL[4] = {0, 5, 11, 16};  // black dots per 16, by shade

inline void packDithered(uint8_t* packed, const uint8_t* shades, uint8_t rotation) {
    // Each source pixel covers two native columns of a write row (portrait
    // ly0, ly1); its two bits depend only on lx & 3, ly0 & 3 and the shade.
    uint8_t pairs[4][4][4];
    for (int lxm = 0; lxm < 4; ++lxm)
        for (int ly0 = 0; ly0 < 4; ++ly0)
            for (int shade = 0; shade < 4; ++shade) {
                const int ly1 = rotation == 3 ? (ly0 + 1) & 3 : (ly0 + 3) & 3;
                const bool b0 = BAYER[ly0][lxm] < BAYER_LEVEL[shade], b1 = BAYER[ly1][lxm] < BAYER_LEVEL[shade];
                pairs[lxm][ly0][shade] = uint8_t((b0 ? 0 : 2) | (b1 ? 0 : 1));
            }
    for (int row = 0; row < WINDOW_ROWS; ++row) {
        // Write row `row` is portrait column lx; native columns walk portrait y
        // (see GameViewport::pack).
        const int lx = rotation == 3 ? WIN_W - 1 - row : row;
        const uint8_t* source = shades + (lx >> 1);
        const uint8_t (*table)[4] = pairs[lx & 3];
        uint8_t* out = packed + row * ROW_BYTES;
        for (int byte = 0; byte < ROW_BYTES; ++byte) {
            uint8_t bits = 0;
            for (int i = 0; i < 4; ++i) {
                const int c = byte * 8 + i * 2;
                const int ly = rotation == 3 ? c : WIN_H - 1 - c;
                bits = uint8_t(bits << 2) | table[ly & 3][source[(ly >> 1) * SOURCE_W] & 3];
            }
            out[byte] = bits;
        }
    }
}

// --- Fast dithered: per-pixel pulse counters (drive_counters.c) --------------
constexpr unsigned DITHER_PULSES_DEFAULT = 8, DITHER_PULSES_MAX = 8;
constexpr int DRIVE_WORDS = PLANE_BYTES / 4;

// Four bit planes count 0..8 independently for 32 pixels at once.
struct DriveCounters {
    uint32_t target[DRIVE_WORDS], count[4][DRIVE_WORDS];
    unsigned pendingPixels;

    void seed(const uint8_t* clean) {
        memcpy(target, clean, PLANE_BYTES);
        memset(count, 0, sizeof(count));
        pendingPixels = 0;
    }
    unsigned prepare(const uint8_t* next8, unsigned blackPulses, unsigned whitePulses, uint8_t* black, uint8_t* white) {
        unsigned pending = 0;
        for (int i = 0; i < DRIVE_WORDS; ++i) {
            uint32_t next;
            memcpy(&next, next8 + i * 4, 4);
            const uint32_t changed = next ^ target[i];
            uint32_t active = 0;
            for (unsigned plane = 0; plane < 4; ++plane) {
                uint32_t value = count[plane][i];
                if (changed) {
                    const uint32_t budget = ((blackPulses & (1u << plane)) ? ~next : 0) |
                                            ((whitePulses & (1u << plane)) ? next : 0);
                    value = (value & ~changed) | (budget & changed);
                    count[plane][i] = value;
                }
                active |= value;
            }
            target[i] = next;
            const uint32_t b = active & ~next, w = active & next;
            memcpy(black + i * 4, &b, 4);
            memcpy(white + i * 4, &w, 4);
            pending += __builtin_popcount(active);
        }
        pendingPixels = pending;
        return pending;
    }
    // Consume only successfully delivered masks, before the next prepare.
    void complete(const uint8_t* black, const uint8_t* white) {
        unsigned pending = 0;
        for (int i = 0; i < DRIVE_WORDS; ++i) {
            uint32_t b, w;
            memcpy(&b, black + i * 4, 4);
            memcpy(&w, white + i * 4, 4);
            uint32_t active = 0;
            for (unsigned plane = 0; plane < 4; ++plane) active |= count[plane][i];
            // Subtract only from delivered, matching counters.
            uint32_t borrow = active & ((b & ~target[i]) | (w & target[i]));
            active = 0;
            for (unsigned plane = 0; plane < 4; ++plane) {
                const uint32_t old = count[plane][i];
                count[plane][i] = old ^ borrow;
                borrow &= ~old;
                active |= count[plane][i];
            }
            pending += __builtin_popcount(active);
        }
        pendingPixels = pending;
    }
};

// --- Fast grayscale (gray_config.h, gray_adaptive.h, gray_plan.c) ------------
// Shades here are InkBoy's K, D, L, W = 0, 1, 2, 3; a level is a model
// (0..192), not measured reflectance.
constexpr unsigned GRAY_DOSE_MAX = 12;
constexpr uint16_t GRAY_HOLDS_DEFAULT = 0xc667;   // bit (source*4 + target): stop after the drive scan
constexpr uint16_t GRAY_HOLD_DIAGONAL = 0x8421;
constexpr bool GRAY_IMMEDIATE_DEFAULT = true;     // pair hold mode
enum { ROUTE_LKL, ROUTE_DKD, ROUTE_DWD, ROUTE_LWL, ROUTE_DLD, ROUTE_LDL, ROUTES };

struct GrayConfig {
    uint8_t dose[4][4];    // [from][to], K D L W
    uint8_t retargetGain;  // 0..4: 50% + 25% each
    uint8_t scanPolicy;    // 0 automatic extra scan, 1 one per slot
};
constexpr GrayConfig GRAY_CONFIG_DEFAULT = {{{0, 2, 4, 7}, {4, 0, 1, 6}, {4, 1, 0, 5}, {7, 3, 2, 0}}, 2, 0};
struct GrayAdaptive {
    uint8_t historyMode;   // 0 off, bit0 completed returns, bit1 interrupted returns
    int8_t returnDelta[ROUTES];
    uint8_t reversePercent[ROUTES];
};
constexpr GrayAdaptive GRAY_ADAPTIVE_DEFAULT = {3, {-1, 0, 0, 0, 0, 0}, {150, 0, 0, 0, 0, 0}};

inline unsigned retargetPercent(const GrayConfig& c) { return 50u + 25u * c.retargetGain; }
inline bool configValid(const GrayConfig& c) {
    if (c.retargetGain > 4 || c.scanPolicy > 1) return false;
    for (int a = 0; a < 4; ++a)
        for (int b = 0; b < 4; ++b)
            if (a == b ? c.dose[a][b] != 0 : c.dose[a][b] < 1 || c.dose[a][b] > GRAY_DOSE_MAX) return false;
    return true;
}
inline bool holdsValid(unsigned mask) { return mask <= 0xffff && (mask & GRAY_HOLD_DIAGONAL) == GRAY_HOLD_DIAGONAL; }
inline bool adaptiveValid(const GrayAdaptive& c) {
    if (c.historyMode > 3) return false;
    for (int i = 0; i < ROUTES; ++i) {
        const unsigned g = c.reversePercent[i];
        if (c.returnDelta[i] < -3 || c.returnDelta[i] > 3 || (g && (g < 50 || g > 200 || g % 25))) return false;
    }
    return true;
}
inline bool adaptiveGrayRoutes(const GrayAdaptive& c) {
    return c.returnDelta[ROUTE_DLD] || c.returnDelta[ROUTE_LDL] || c.reversePercent[ROUTE_DLD] ||
           c.reversePercent[ROUTE_LDL];
}
inline int adaptiveRoute(unsigned from, unsigned via, unsigned to) {
    if (from != to) return -1;
    if (from == 2 && via == 0) return ROUTE_LKL;
    if (from == 1 && via == 0) return ROUTE_DKD;
    if (from == 1 && via == 3) return ROUTE_DWD;
    if (from == 2 && via == 3) return ROUTE_LWL;
    if (from == 1 && via == 2) return ROUTE_DLD;
    if (from == 2 && via == 1) return ROUTE_LDL;
    return -1;
}

// Pixel state, InkBoy's gray_pixel_t: level (8) remaining (4) target (2) source (2).
struct GrayHistory { uint8_t origin, blackSteps, whiteSteps; };  // origin 4 = unclassified

class GrayPlan {
public:
    uint16_t* pixel = nullptr;    // PIXELS entries
    GrayHistory* history = nullptr;  // optional, PIXELS entries
    uint8_t active[MASK_BYTES];
    uint8_t previousGame[SOURCE_BYTES];
    unsigned pending = 0;
    GrayConfig config = GRAY_CONFIG_DEFAULT;
    GrayAdaptive adaptive = GRAY_ADAPTIVE_DEFAULT;
    bool hadGame = false;
    unsigned historyReturns = 0, historyReversals = 0;

    static uint8_t levelOf(uint16_t s) { return s & 255; }
    static uint8_t remainingOf(uint16_t s) { return (s >> 8) & 15; }
    static uint8_t targetOf(uint16_t s) { return (s >> 12) & 3; }
    static uint8_t sourceOf(uint16_t s) { return s >> 14; }
    static uint16_t make(unsigned level, unsigned remaining, unsigned target, unsigned source) {
        return uint16_t(level | remaining << 8 | target << 12 | source << 14);
    }

    // Every pixel settled at `shade` (the window after a full refresh).
    void seed(uint8_t shade) {
        initPulseSteps();
        if (!configValid(config)) config = GRAY_CONFIG_DEFAULT;
        for (int i = 0; i < PIXELS; ++i) {
            pixel[i] = make(shade * 64, 0, shade, shade);
            if (history) history[i] = {4, 0, 0};
        }
        pending = 0;
        memset(active, 0, sizeof(active));
        hadGame = false;
        historyReturns = historyReversals = 0;
    }

    // New targets from a 2-bit plan-order game image (Peanut shades).
    void target(const uint8_t* game) {
        Policy policy;
        policy.config = config;
        policy.adaptive = adaptive;
        policy.historyMode = adaptive.historyMode;
        policy.historyReturns = historyReturns;
        policy.historyReversals = historyReversals;
        for (unsigned level = 0; level <= 192; ++level) {
            const unsigned low = level / 64, fraction = level % 64, high = low < 3 ? low + 1 : low;
            for (unsigned t = 0; t < 4; ++t)
                policy.budget[level][t] = (config.dose[low][t] * (64 - fraction) + config.dose[high][t] * fraction + 63) / 64;
        }
        for (unsigned route = 0; route <= ROUTES; ++route) {
            const unsigned gain = route ? adaptive.reversePercent[route - 1] : retargetPercent(config);
            for (unsigned budget = 0; budget <= GRAY_DOSE_MAX; ++budget) {
                const unsigned value = (budget * gain + 50) / 100;
                policy.scaled[route][budget] = value < 1 ? 1 : value > GRAY_DOSE_MAX ? GRAY_DOSE_MAX : value;
            }
        }
        for (int g = 0; g < SOURCE_BYTES; ++g) {
            const uint8_t incoming = game[g];
            if (hadGame && incoming == previousGame[g]) continue;
            for (int k = 0; k < 4; ++k) {
                const int i = g * 4 + k;
                const uint8_t bit = 128u >> (i % 8);
                const unsigned t = 3 - ((incoming >> (6 - 2 * k)) & 3);  // Peanut shade -> K D L W
                uint16_t value = pixel[i];
                if (t == targetOf(value)) continue;
                GrayHistory h = {0, 0, 0};
                if (history) h = history[i];
                const bool wasPending = remainingOf(value) != 0;
                retarget(policy, value, history ? &h : nullptr, t);
                pixel[i] = value;
                if (history) history[i] = h;
                if (remainingOf(value)) {
                    active[i / 8] |= bit;
                    if (!wasPending) ++pending;
                } else {
                    active[i / 8] &= uint8_t(~bit);
                    if (wasPending) --pending;
                }
            }
            previousGame[g] = incoming;
        }
        hadGame = true;
        historyReturns = policy.historyReturns;
        historyReversals = policy.historyReversals;
    }

    // Drive masks for the next scan, plus (when `followBlack`) the follow-up
    // scan's: only HOLD-off transitions with at least two pulses left drive again.
    unsigned masks(uint8_t* black, uint8_t* white, uint8_t* followBlack = nullptr, uint8_t* followWhite = nullptr,
                   unsigned holdMask = 0xffff) const {
        unsigned count = 0;
        for (int byte = 0; byte < MASK_BYTES; ++byte) {
            unsigned bits = active[byte], b = 0, w = 0, fb = 0, fw = 0;
            while (bits) {
                const unsigned shift = __builtin_ctz(bits), bit = 1u << shift;
                const unsigned state = pixel[byte * 8 + 7 - shift];
                const unsigned t = (state >> 12) & 3, source = state >> 14, remaining = (state >> 8) & 15;
                const bool lighten = t * 64 > (state & 255);
                if (lighten) w |= bit; else b |= bit;
                if (followBlack && remaining > 1 && !(holdMask & (1u << (source * 4u + t)))) {
                    if (lighten) fw |= bit; else fb |= bit;
                }
                bits &= ~bit;
                ++count;
            }
            black[byte] = b; white[byte] = w;
            if (followBlack) { followBlack[byte] = fb; followWhite[byte] = fw; }
        }
        return count;
    }

    // Commit delivered masks (both scans of a pair when given).
    void complete(const uint8_t* black, const uint8_t* white, const uint8_t* followBlack = nullptr,
                  const uint8_t* followWhite = nullptr) {
        const bool grayRoutes = adaptiveGrayRoutes(adaptive);
        for (int byte = 0; byte < MASK_BYTES; ++byte) {
            const unsigned fb = followBlack ? followBlack[byte] : 0, fw = followWhite ? followWhite[byte] : 0;
            unsigned act = active[byte];
            unsigned bits = (black[byte] | white[byte] | fb | fw) & act;
            while (bits) {
                const unsigned shift = __builtin_ctz(bits), bit = 1u << shift, i = byte * 8 + 7 - shift;
                const unsigned state = pixel[i], shade = (state >> 12) & 3;
                const unsigned b = black[byte] & bit, w = white[byte] & bit;
                int level = state & 255;
                const int distance = int(shade) * 64 - level;
                unsigned remaining = (state >> 8) & 15;
                unsigned steps = !(b && w) && ((distance > 0 && w) || (distance < 0 && b));
                if (!((fb & bit) && (fw & bit)) && ((distance > 0 && (fw & bit)) || (distance < 0 && (fb & bit)))) ++steps;
                const unsigned delivered = steps < remaining ? steps : remaining;
                if (delivered) {
                    level = pulseLevel()[shade][level][remaining][delivered - 1];
                    remaining -= delivered;
                    if (!remaining) { act &= ~bit; --pending; }
                    pixel[i] = uint16_t((state & 0xf000u) | (remaining << 8) | unsigned(level));
                    if (history && (shade == 0 || shade == 3 || grayRoutes)) {
                        uint8_t* counter = distance < 0 ? &history[i].blackSteps : &history[i].whiteSteps;
                        const unsigned c = *counter + delivered;
                        *counter = c > 255 ? 255 : c;
                    }
                }
                bits &= ~bit;
            }
            active[byte] = act;
        }
    }

private:
    struct Policy {
        GrayConfig config;
        GrayAdaptive adaptive;
        unsigned historyMode, historyReturns, historyReversals;
        uint8_t budget[193][4], scaled[ROUTES + 1][GRAY_DOSE_MAX + 1];
    };
    // Exact one/two-pulse progression table.
    typedef uint8_t PulseTable[4][193][GRAY_DOSE_MAX + 1][2];
    static PulseTable& pulseLevel() { static PulseTable table; return table; }
    static void initPulseSteps() {
        static bool ready = false;
        if (ready) return;
        PulseTable& t = pulseLevel();
        for (int target = 0; target < 4; ++target)
            for (int level = 0; level <= 192; ++level)
                for (int n = 1; n <= int(GRAY_DOSE_MAX); ++n) {
                    const int first = level + (target * 64 - level) / n;
                    t[target][level][n][0] = first;
                    t[target][level][n][1] = n > 1 ? first + (target * 64 - first) / (n - 1) : first;
                }
        ready = true;
    }

    static void retarget(Policy& plan, uint16_t& p, GrayHistory* h, unsigned target) {
        const GrayConfig& config = plan.config;
        const unsigned level = levelOf(p), pTarget = targetOf(p), pRemaining = remainingOf(p);
        // Interpolate doses between adjacent model shades, using delivered
        // progress rather than an abandoned target as the settled source.
        const unsigned base = plan.budget[level][target];
        unsigned budget = pRemaining && base ? plan.scaled[0][base] : base;
        if (h) {
            const bool grayRoutes = adaptiveGrayRoutes(plan.adaptive);
            const unsigned steps = pTarget == 0 ? h->blackSteps : pTarget == 3 ? h->whiteSteps
                : grayRoutes && h->origin < 4 ? (pTarget < h->origin ? h->blackSteps : h->whiteSteps) : 0;
            const int route = steps ? adaptiveRoute(h->origin, pTarget, target) : -1;
            if (route >= 0) {
                if (!pRemaining && (plan.historyMode & 1) && level == pTarget * 64 &&
                    steps == config.dose[h->origin][pTarget]) {
                    const int delta = plan.adaptive.returnDelta[route];
                    if (delta) {
                        const int adjusted = int(base) + delta;
                        budget = adjusted < 1 ? 1 : adjusted > int(GRAY_DOSE_MAX) ? GRAY_DOSE_MAX : unsigned(adjusted);
                        ++plan.historyReturns;
                    }
                } else if (pRemaining && base && (plan.historyMode & 2)) {
                    const unsigned gain = plan.adaptive.reversePercent[route];
                    if (gain) { budget = plan.scaled[route + 1][base]; ++plan.historyReversals; }
                }
            }
            h->origin = (target == 0 || target == 3 || grayRoutes) && !pRemaining && level % 64 == 0 ? level / 64 : 4;
            h->blackSteps = h->whiteSteps = 0;
        }
        // Freeze the nearest estimated source for this transition.
        const unsigned source = (level + 32u) / 64u;
        p = make(level, budget, target, source);
    }
};

// Masks (1 bit per source pixel, plan order) -> the controller's planes for
// one scan. SSD1677 waveform rows by (old, new): black drive (1, 0), white
// drive (0, 1), hold (1, 1). Each source pixel covers its 2x2 panel cell.
inline void masksToPlanes(const uint8_t* black, const uint8_t* white, uint8_t* newPlane, uint8_t* oldPlane) {
    static const uint8_t SPREAD[16] = {0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F,
                                       0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF};
    constexpr int COLUMN_MASK_BYTES = SOURCE_H / 8;  // 18
    for (int k = 0; k < SOURCE_W; ++k) {
        uint8_t* n0 = newPlane + 2 * k * ROW_BYTES;
        uint8_t* o0 = oldPlane + 2 * k * ROW_BYTES;
        for (int m = 0; m < COLUMN_MASK_BYTES; ++m) {
            const uint8_t b = black[k * COLUMN_MASK_BYTES + m], w = white[k * COLUMN_MASK_BYTES + m];
            n0[2 * m] = uint8_t(~SPREAD[b >> 4]);
            n0[2 * m + 1] = uint8_t(~SPREAD[b & 15]);
            o0[2 * m] = uint8_t(~SPREAD[w >> 4]);
            o0[2 * m + 1] = uint8_t(~SPREAD[w & 15]);
        }
        memcpy(n0 + ROW_BYTES, n0, ROW_BYTES);
        memcpy(o0 + ROW_BYTES, o0, ROW_BYTES);
    }
}
// Dithered masks are already in panel layout: black/white bits per panel pixel.
inline void panelMasksToPlanes(const uint8_t* black, const uint8_t* white, uint8_t* newPlane, uint8_t* oldPlane) {
    for (int i = 0; i < PLANE_BYTES; ++i) {
        newPlane[i] = uint8_t(~black[i]);
        oldPlane[i] = uint8_t(~white[i]);
    }
}

// --- Transition test (gray_motion.c) -----------------------------------------
// Four bands (K, D, L, W from the top), three 12-pixel rectangles of the other
// shades each, held for 1 s, moving left to right over 5 s, then 1 s at the
// right end. Peanut shades (0 white .. 3 black).
constexpr int64_t MOTION_TRAVEL_US = 5000000, MOTION_LEAD_US = 1000000, MOTION_CYCLE_US = 6000000;
constexpr unsigned MOTION_WIDTH = 12, MOTION_MAX_X = SOURCE_W - MOTION_WIDTH;
inline unsigned motionX(int64_t elapsed) {
    if (elapsed < MOTION_LEAD_US) return 0;
    const int64_t phase = (elapsed - MOTION_LEAD_US) % MOTION_CYCLE_US;
    if (phase >= MOTION_TRAVEL_US) return MOTION_MAX_X;
    return unsigned(phase * MOTION_MAX_X / MOTION_TRAVEL_US);
}
inline unsigned motionPatch(unsigned background, unsigned lane) { return lane < background ? lane : lane + 1; }
inline void motionRender(uint8_t* shades, unsigned x) {
    if (x > MOTION_MAX_X) x = MOTION_MAX_X;
    for (unsigned bg = 0; bg < 4; ++bg) {
        // InkBoy band bg shows K D L W = bg; Peanut shade = 3 - bg.
        memset(shades + bg * 36 * SOURCE_W, 3 - bg, 36 * SOURCE_W);
        for (unsigned lane = 0; lane < 3; ++lane) {
            const uint8_t shade = 3 - motionPatch(bg, lane);
            for (unsigned y = bg * 36 + lane * 12 + 2; y < bg * 36 + lane * 12 + 10; ++y)
                memset(shades + y * SOURCE_W + x, shade, MOTION_WIDTH);
        }
    }
}

// --- Pacing (fast_display.h) ---------------------------------------------------
constexpr int64_t IMAGE_PERIOD_US = 33334;
inline int64_t drivePeriodUs(bool gray) { return gray ? 15000 : 30000; }
inline int64_t nextImage(int64_t deadline, int64_t now) {
    if (!deadline) return now + IMAGE_PERIOD_US;
    if (now < deadline) return deadline;
    // Keep the image clock's phase; skip missed deadlines, never queue old frames.
    return deadline + ((now - deadline) / IMAGE_PERIOD_US + 1) * IMAGE_PERIOD_US;
}
// Wake time that leaves room for a whole completion scan before the next
// image; a new image takes priority when none fits.
inline int64_t wakeTime(int64_t now, int64_t ready, int64_t deadline, bool pending, int64_t cost) {
    const int64_t earliest = now > ready ? now : ready;
    if (now >= deadline || (pending && earliest + cost <= deadline)) return earliest;
    return deadline > earliest ? deadline : earliest;
}
constexpr int64_t IDLE_POWER_OFF_US = 750000;

// FPS digits stamped in the top-left source corner (Show FPS).
inline void stampFps(uint8_t* shades, unsigned fps) {
    static const uint16_t DIGITS[10] = {0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9,
                                        0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF};
    if (fps > 99) fps = 99;
    const unsigned d[2] = {fps / 10, fps % 10};
    for (int y = 0; y < 7; ++y) memset(shades + y * SOURCE_W, 0, 10);
    for (int i = 0; i < 2; ++i)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 3; ++x)
                if (DIGITS[d[i]] & (1 << (14 - (y * 3 + x)))) shades[(y + 1) * SOURCE_W + 1 + i * 4 + x] = 3;
}

// --- Settings ---------------------------------------------------------------------
enum Mode : uint8_t { MODE_DITHERED = 0, MODE_GRAYSCALE = 1 };
struct Settings {
    uint8_t mode;
    bool showFps;
    uint8_t blackPulses, whitePulses;  // dithered, 1..8
    GrayConfig gray;
    uint16_t holds;
    bool immediate;                    // pair hold mode
    GrayAdaptive adaptive;
};
constexpr Settings DEFAULT_SETTINGS = {MODE_GRAYSCALE, false, DITHER_PULSES_DEFAULT, DITHER_PULSES_DEFAULT,
                                       GRAY_CONFIG_DEFAULT, GRAY_HOLDS_DEFAULT, GRAY_IMMEDIATE_DEFAULT,
                                       GRAY_ADAPTIVE_DEFAULT};
inline void sanitize(Settings& s) {
    s.mode = s.mode == MODE_DITHERED ? MODE_DITHERED : MODE_GRAYSCALE;
    if (s.blackPulses < 1 || s.blackPulses > DITHER_PULSES_MAX) s.blackPulses = DITHER_PULSES_DEFAULT;
    if (s.whitePulses < 1 || s.whitePulses > DITHER_PULSES_MAX) s.whitePulses = DITHER_PULSES_DEFAULT;
    if (!configValid(s.gray)) s.gray = GRAY_CONFIG_DEFAULT;
    if (!holdsValid(s.holds)) s.holds = GRAY_HOLDS_DEFAULT;
    if (!adaptiveValid(s.adaptive)) s.adaptive = GRAY_ADAPTIVE_DEFAULT;
}
}  // namespace InkBoyDrive
