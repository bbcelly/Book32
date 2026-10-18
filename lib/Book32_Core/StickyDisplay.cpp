#include "StickyDisplay.h"

#if defined(BOARD_SEEED_STICKY)

#include "Config.h"
#include "GameViewport.h"
#include "InkBoyDrive.h"
#include <esp_timer.h>
#include "StickyGrayImage.h"
#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>

namespace {
constexpr size_t FRAME_BYTES = ((800 + 7) / 8) * 480;

// --- Inkade fast video (port of kuslota/InkBoy, see InkBoyDrive.h) ----------
// One waveform frame at frame-rate setting 0x88 lasts 5 ms (measured), and
// display-only updates (0x22 = 0x0C) skip the ~82 ms analog power-up.
// InkBoy's single active frame drives about 20 ms in dithered mode and about
// 14 ms in grayscale: 4 and 3 frames here.
constexpr uint32_t GAME_SPI_HZ = 20000000;
constexpr uint8_t GAME_FRAME_RATE = 0x88;
constexpr uint8_t GAME_DITHER_FRAMES = 4;
constexpr uint8_t GAME_GRAY_FRAMES = 3;
constexpr size_t GAME_BYTES = GameViewport::PACKED_BYTES;
static_assert(GAME_BYTES == size_t(InkBoyDrive::PLANE_BYTES), "InkBoy drive uses the game window layout");

// The only SSD1677 commands the game driver may send. Everything here works
// on volatile registers and RAM; nothing programs OTP or other one-time
// memory (0x2A VCOM OTP, 0x36 OTP program, 0x37/0x38 OTP registers are
// excluded). gameCommand<> refuses anything else at compile time.
constexpr bool gameCommandAllowed(uint8_t command) {
    return command == 0x11 ||                    // data entry mode
           command == 0x20 ||                    // master activation
           command == 0x21 ||                    // display update control 1
           command == 0x22 ||                    // display update control 2
           command == 0x24 || command == 0x26 || // new / old image RAM
           command == 0x32 ||                    // volatile waveform LUT
           command == 0x3C ||                    // border waveform
           command == 0x44 || command == 0x45 || // RAM window
           command == 0x4E || command == 0x4F;   // RAM address
}
// Display update control 2 sequences the game driver uses.
constexpr bool gameUpdateAllowed(uint8_t value) {
    return value == 0x0C ||  // display only (analog already on)
           value == 0xCC ||  // clock + analog on, display
           value == 0x03;    // analog + clock off
}

// SSD1677 105-byte register waveform (same layout as GxEPD2's SSD1677 drivers):
// rows LUT0..LUT4 select voltages per phase, then phase timings, then frame
// rates. LUT index = old * 2 + new with RAM 1 = white; VSL drives white and
// VSH1 black. Unchanged pixels (LUT0, LUT3) are never driven.
void buildGameLut(uint8_t lut[105], uint8_t pulseFrames) {
    memset(lut, 0, 105);
    lut[10] = 0x02;                  // LUT1 black -> white: group 0 phase D = VSL
    lut[20] = 0x01;                  // LUT2 white -> black: group 0 phase D = VSH1
    lut[50 + 3] = pulseFrames;       // group 0 timing: TPA, TPB, TPC, TPD, RP
    for (int i = 100; i < 105; ++i) lut[i] = GAME_FRAME_RATE;
}
// Still images: every pixel whose old and new bits differ gets a black pulse,
// whichever way round bb_epaper's plane writes store black and white.
void buildBlackPulseLut(uint8_t lut[105], uint8_t pulseFrames) {
    buildGameLut(lut, pulseFrames);
    lut[10] = 0x01;                  // LUT1 also VSH1 (black)
}
}

StickyDisplay::StickyDisplay() : GFXcanvas1(800, 480), _panel(EP397_800x480) {
    _previousBuffer = static_cast<uint8_t*>(
        heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!_previousBuffer) _previousBuffer = static_cast<uint8_t*>(malloc(FRAME_BYTES));
    if (_previousBuffer) memset(_previousBuffer, 0xFF, FRAME_BYTES);
}

void StickyDisplay::init(uint32_t, bool, uint16_t, bool) {
    pinMode(EPD_ENABLE, OUTPUT);
    digitalWrite(EPD_ENABLE, HIGH);
    pinMode(EPD_CS, OUTPUT);
    digitalWrite(EPD_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
    _panel.initIO(EPD_DC, EPD_RST, EPD_BUSY, EPD_CS, EPD_MOSI, EPD_SCK, 8000000);
    _panel.setBuffer(getBuffer());
    clearScreen();
}

void StickyDisplay::setFullWindow() {
    _partialWindow = false;
}

void StickyDisplay::setPartialWindow(int16_t, int16_t, int16_t, int16_t) {
    // InkDeck draws a complete composited frame even for a partial update. Keep
    // the full buffer valid, but select the panel's low-flash partial waveform.
    _partialWindow = true;
}

void StickyDisplay::firstPage() {
    _pagePending = true;
}

bool StickyDisplay::nextPage() {
    if (_pagePending) {
        flush();
        _pagePending = false;
    }
    return false;
}

void StickyDisplay::flush() {
    if (!getBuffer()) return;
    // After game video the panel's game window no longer matches
    // _previousBuffer, so the next update must not be a differential one.
    if (_forceFullRefresh) {
        _partialWindow = false;
        _forceFullRefresh = false;
    }
    digitalWrite(SD_CS, HIGH);
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    if (_partialWindow && _previousBuffer) {
        // SSD1677 partial refresh compares an old plane with a new plane.
        // Supplying both avoids the forced all-pixel update (and its flash)
        // while still driving erased black pixels cleanly back to white.
        _panel.setBuffer(_previousBuffer);
        // This is a standalone one-frame buffer. PLANE_1 reads a second
        // frame at buffer + FRAME_BYTES, outside our allocation. Copy this
        // buffer's first frame to the controller's old-image plane instead.
        _panel.writePlane(PLANE_0_TO_1);
        _panel.setBuffer(getBuffer());
        _panel.writePlane(PLANE_0);
    } else {
        _panel.setBuffer(getBuffer());
        _panel.writePlane(_partialWindow ? PLANE_FALSE_DIFF : PLANE_DUPLICATE);
    }
    _panel.refresh(_partialWindow ? REFRESH_PARTIAL : REFRESH_FULL, false);
    SPI.endTransaction();
    _panel.wait();
    if (_previousBuffer) memcpy(_previousBuffer, getBuffer(), FRAME_BYTES);
}

void StickyDisplay::refresh(bool partialUpdateMode) {
    _partialWindow = partialUpdateMode;
    flush();
}

void StickyDisplay::clearScreen(uint8_t value) {
    if (!getBuffer()) return;
    memset(getBuffer(), value, ((800 + 7) / 8) * 480);
    _partialWindow = false;
    flush();
}

// --- Inkade fast video -----------------------------------------------------

void StickyDisplay::gameSend(uint8_t command, const uint8_t* data, size_t length) {
    SPI.beginTransaction(SPISettings(GAME_SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(SD_CS, HIGH);
    digitalWrite(EPD_DC, LOW);
    digitalWrite(EPD_CS, LOW);
    SPI.transfer(command);
    digitalWrite(EPD_DC, HIGH);
    if (data && length) SPI.writeBytes(data, length);
    digitalWrite(EPD_CS, HIGH);
    SPI.endTransaction();
}

template <uint8_t Command>
void StickyDisplay::gameCommand(const uint8_t* data, size_t length) {
    static_assert(gameCommandAllowed(Command), "SSD1677 command is not on the game driver's allowlist");
    gameSend(Command, data, length);
}

template <uint8_t Value>
void StickyDisplay::gameUpdate() {
    static_assert(gameUpdateAllowed(Value), "display update sequence is not on the allowlist");
    gameCommandByte<0x22>(Value);
    gameCommand<0x20>();
}

// Sets the RAM window and address to the game window's first write row.
void StickyDisplay::gameWindow() {
    const int x0 = GameViewport::nativeX(_gameRotation), x1 = x0 + GameViewport::H - 1;
    const int y0 = GameViewport::nativeY0(_gameRotation), y1 = y0 + GameViewport::WINDOW_ROWS - 1;
    const uint8_t xRange[4] = {uint8_t(x0), uint8_t(x0 >> 8), uint8_t(x1), uint8_t(x1 >> 8)};
    const uint8_t yRange[4] = {uint8_t(y1), uint8_t(y1 >> 8), uint8_t(y0), uint8_t(y0 >> 8)};
    const uint8_t xStart[2] = {uint8_t(x0), uint8_t(x0 >> 8)};
    const uint8_t yStart[2] = {uint8_t(y1), uint8_t(y1 >> 8)};
    gameCommandByte<0x11>(0x01);  // X increment, Y decrement
    gameCommand<0x44>(xRange, sizeof(xRange));
    gameCommand<0x45>(yRange, sizeof(yRange));
    gameCommand<0x4E>(xStart, sizeof(xStart));
    gameCommand<0x4F>(yStart, sizeof(yStart));
}

bool StickyDisplay::gameWaitBusy() {
    const uint32_t start = millis();
    while (digitalRead(EPD_BUSY) == HIGH) {
        if (millis() - start > 2000) {
            Serial.println("StickyDisplay: game update busy timeout");
            return false;
        }
    }
    return true;
}

// --- InkBoyPanel: the SSD1677 side of the InkBoy drive -----------------------

bool StickyDisplay::videoStart(bool gray) {
    // Entered right after a full refresh: the window shows white, and the
    // controller holds the OTP voltages/VCOM loaded by it. InkBoy: one active
    // waveform frame, ~20 ms dithered, ~14 ms grayscale.
    uint8_t lut[105];
    buildGameLut(lut, gray ? GAME_GRAY_FRAMES : GAME_DITHER_FRAMES);
    // bb_epaper's full-refresh init sets Display Update Control 1 to 0x40,
    // which makes the controller read the old RAM as zeros; restore normal.
    const uint8_t updateControl[2] = {0x00, 0x00};
    gameCommand<0x21>(updateControl, sizeof(updateControl));
    gameCommandByte<0x3C>(0xC0);  // border floats during updates
    gameCommand<0x32>(lut, sizeof(lut));
    _gamePowered = false;
    return true;
}

bool StickyDisplay::videoScan(const uint8_t* oldPlane, const uint8_t* newPlane) {
    gameWindow();
    gameCommand<0x26>(oldPlane, GAME_BYTES);
    gameWindow();
    gameCommand<0x24>(newPlane, GAME_BYTES);
    if (_gamePowered) gameUpdate<0x0C>();
    else gameUpdate<0xCC>();
    _gamePowered = true;
    return gameWaitBusy();
}

void StickyDisplay::videoPowerOff() {
    if (!_gamePowered) return;
    gameUpdate<0x03>();  // analog + clock off; RAM and waveform are kept
    gameWaitBusy();
    _gamePowered = false;
}

void StickyDisplay::setGameMode(bool active) {
    if (active == _gaming) return;
    if (active) {
        _gameRotation = getRotation() == 1 ? 1 : 3;
        _gaming = _video.start(this);
    } else {
        _video.stop();
        _gaming = false;
        // bb_epaper reloads the OTP waveform on its next refresh; make it full.
        _forceFullRefresh = true;
    }
}

// --- Live frames (gestures) --------------------------------------------------
// The algorithm of InkBoyDrive::DriveCounters over the whole panel: a pixel
// that changes gets LIVE_PULSES one-frame (~20 ms) drives, one per scan,
// toward its new color, so a moving picture stays readable while earlier
// changes finish. Planes are the canvas layout, sent with the same RAM window
// bb_epaper uses for full frames.
namespace {
constexpr int LIVE_WORDS = FRAME_BYTES / 4;
constexpr uint32_t LIVE_PULSES = 6;
}

void StickyDisplay::liveWindow() {
    const uint8_t xRange[4] = {0x00, 0x00, 0x1F, 0x03};  // 0..799
    const uint8_t yRange[4] = {0xDF, 0x01, 0x00, 0x00};  // 479..0
    const uint8_t zero[2] = {0x00, 0x00};
    gameCommandByte<0x11>(0x01);  // X increment, Y decrement
    gameCommand<0x44>(xRange, sizeof(xRange));
    gameCommand<0x45>(yRange, sizeof(yRange));
    gameCommand<0x4E>(zero, sizeof(zero));
    gameCommand<0x4F>(zero, sizeof(zero));
}

bool StickyDisplay::liveBegin() {
    if (_live) return true;
    if (_gaming || !getBuffer() || !_previousBuffer) return false;
    auto psram = [](size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); };
    if (!_liveTarget) _liveTarget = static_cast<uint32_t*>(psram(FRAME_BYTES));
    if (!_liveCount) _liveCount = static_cast<uint32_t*>(psram(4 * FRAME_BYTES));
    // PSRAM sends as fast as internal RAM here (the SPI clock is the limit).
    if (!_liveOld) _liveOld = static_cast<uint8_t*>(psram(FRAME_BYTES));
    if (!_liveNew) _liveNew = static_cast<uint8_t*>(psram(FRAME_BYTES));
    if (!_liveTarget || !_liveCount || !_liveOld || !_liveNew) return false;
    // The panel shows the last normal refresh.
    memcpy(_liveTarget, _previousBuffer, FRAME_BYTES);
    memset(_liveCount, 0, 4 * FRAME_BYTES);
    _livePending = 0;
    _liveBusy = false;
    _liveWaitUs = _liveCalcUs = _liveSendUs = _liveScans = 0;
    videoStart(false);  // Inkade's dithered one-frame waveform
    _live = true;
    return true;
}

bool StickyDisplay::liveFrame(const uint8_t* frame) {
    if (!_live) return false;
    // The counters are worked out while the panel still drives the last
    // scan (its data is already in the controller); only sending waits.
    const int64_t calcStart = esp_timer_get_time();
    const uint8_t* canvas = frame ? frame : getBuffer();
    uint32_t* count0 = _liveCount;
    uint32_t* count1 = count0 + LIVE_WORDS;
    uint32_t* count2 = count1 + LIVE_WORDS;
    uint32_t* count3 = count2 + LIVE_WORDS;
    unsigned pending = 0;
    for (int i = 0; i < LIVE_WORDS; ++i) {
        uint32_t next;
        memcpy(&next, canvas + i * 4, 4);
        uint32_t c0 = count0[i], c1 = count1[i], c2 = count2[i], c3 = count3[i];
        const uint32_t changed = next ^ _liveTarget[i];
        if (changed) {  // Changed pixels start their pulses again
            _liveTarget[i] = next;
            c0 = (c0 & ~changed) | ((LIVE_PULSES & 1) ? changed : 0);
            c1 = (c1 & ~changed) | ((LIVE_PULSES & 2) ? changed : 0);
            c2 = (c2 & ~changed) | ((LIVE_PULSES & 4) ? changed : 0);
            c3 = (c3 & ~changed) | ((LIVE_PULSES & 8) ? changed : 0);
        }
        const uint32_t active = c0 | c1 | c2 | c3;
        // Old/new per pixel: 1/0 drives black, 0/1 white, equal holds.
        const uint32_t newWord = ~(active & ~next), oldWord = ~(active & next);
        memcpy(_liveNew + i * 4, &newWord, 4);
        memcpy(_liveOld + i * 4, &oldWord, 4);
        // One pulse given: count down the active pixels.
        uint32_t borrow = active, old;
        old = c0; c0 ^= borrow; borrow &= ~old;
        old = c1; c1 ^= borrow; borrow &= ~old;
        old = c2; c2 ^= borrow; borrow &= ~old;
        c3 ^= borrow;
        count0[i] = c0; count1[i] = c1; count2[i] = c2; count3[i] = c3;
        pending += __builtin_popcount(c0 | c1 | c2 | c3);
    }
    _livePending = pending;
    const int64_t waitStart = esp_timer_get_time();
    _liveCalcUs += waitStart - calcStart;
    if (_liveBusy && !gameWaitBusy()) return false;
    _liveBusy = false;
    const int64_t sendStart = esp_timer_get_time();
    _liveWaitUs += sendStart - waitStart;
    liveWindow();
    gameCommand<0x26>(_liveOld, FRAME_BYTES);
    liveWindow();
    gameCommand<0x24>(_liveNew, FRAME_BYTES);
    if (_gamePowered) gameUpdate<0x0C>();
    else gameUpdate<0xCC>();
    _gamePowered = true;
    _liveBusy = true;  // Drives while the caller draws the next frame
    _liveSendUs += esp_timer_get_time() - sendStart;
    _liveScans++;
    return true;
}

void StickyDisplay::liveEnd() {
    if (!_live) return;
    for (int i = 0; i < 12 && _livePending; ++i) liveFrame();  // Finish every pulse
    if (_liveBusy) gameWaitBusy();
    _liveBusy = false;
    videoPowerOff();
    _live = false;
    if (_liveScans)
        Serial.printf("StickyDisplay: %u live scans, per scan: wait %lu us, counters %lu us, send %lu us\n",
                      unsigned(_liveScans), static_cast<unsigned long>(_liveWaitUs / _liveScans),
                      static_cast<unsigned long>(_liveCalcUs / _liveScans),
                      static_cast<unsigned long>(_liveSendUs / _liveScans));
    // The panel now shows the canvas: the reference for the next partial.
    memcpy(_previousBuffer, _liveTarget, FRAME_BYTES);
}

// --- Grayscale still images --------------------------------------------------

bool StickyDisplay::drawGrayCover(uint8_t* gray, int x, int y, int w, int h) {
    int16_t* errors = static_cast<int16_t*>(ps_malloc(sizeof(int16_t) * 2 * (w + 2)));
    if (!errors) return false;
    StickyGrayImage::quantize(gray, w, h, errors);
    free(errors);
    return drawGrayImage(gray, x, y, w, h);
}

bool StickyDisplay::drawGrayImage(const uint8_t* pulses, int x, int y, int w, int h) {
    if (!getBuffer() || !_previousBuffer || !pulses || _gaming) return false;
    uint8_t maxPulses = 0;
    for (size_t i = 0, n = size_t(w) * h; i < n; ++i) maxPulses = std::max(maxPulses, pulses[i]);
    if (!maxPulses) return true;
    // The canvas is borrowed to lay out each pass in controller order (GFX
    // handles the rotation); keep its picture to restore afterwards.
    uint8_t* saved = static_cast<uint8_t*>(heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!saved) return false;
    memcpy(saved, getBuffer(), FRAME_BYTES);

    const uint32_t started = millis();
    uint8_t lut[105];
    buildBlackPulseLut(lut, StickyGrayImage::PULSE_FRAMES);
    const uint8_t updateControl[2] = {0x00, 0x00};
    // Old plane all white: a black pixel in the new plane differs from it and
    // gets one black pulse; every white one is held.
    memset(_previousBuffer, 0xFF, FRAME_BYTES);
    bool delivered = true;
    for (uint8_t pass = 1; pass <= maxPulses && delivered; ++pass) {
        fillScreen(1);
        for (int row = 0; row < h; ++row) {
            const uint8_t* line = pulses + size_t(row) * w;
            for (int col = 0; col < w; ++col)
                if (line[col] >= pass) drawPixel(x + col, y + row, 0);
        }
        digitalWrite(SD_CS, HIGH);
        SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
        _panel.setBuffer(_previousBuffer);
        _panel.writePlane(PLANE_0_TO_1);
        _panel.setBuffer(getBuffer());
        _panel.writePlane(PLANE_0);
        SPI.endTransaction();
        // bb_epaper re-sends this panel's full init before each plane write,
        // which resets update control and the waveform: set them per pass.
        gameCommand<0x21>(updateControl, sizeof(updateControl));
        gameCommandByte<0x3C>(0xC0);
        gameCommand<0x32>(lut, sizeof(lut));
        // The init also resets the controller, which turns the analog supply
        // off: power it up for every pass.
        gameUpdate<0xCC>();
        delivered = gameWaitBusy();
    }
    gameUpdate<0x03>();  // analog + clock off
    gameWaitBusy();
    memcpy(getBuffer(), saved, FRAME_BYTES);
    free(saved);
    // The panel no longer matches the canvas differential reference.
    memset(_previousBuffer, 0xFF, FRAME_BYTES);
    _forceFullRefresh = true;
    Serial.printf("StickyDisplay: grayscale image %dx%d, %u passes in %lu ms\n", w, h, maxPulses,
                  millis() - started);
    return delivered;
}

void StickyDisplay::hibernate() {
    // The image remains on e-paper without power. Put the SSD1677 into its
    // controller-level deep-sleep state before the board power manager gates
    // the display rail.
    digitalWrite(SD_CS, HIGH);
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    _panel.sleep(1);
    SPI.endTransaction();
}

#endif
