#include "StickyDisplay.h"

#if defined(BOARD_SEEED_STICKY)

#include "Config.h"
#include "GameViewport.h"
#include <cstring>
#include <esp_heap_caps.h>

namespace {
constexpr size_t FRAME_BYTES = ((800 + 7) / 8) * 480;

// --- Ink Boy fast video -----------------------------------------------------
// Measured on hardware: one waveform frame at frame-rate setting 0x88 lasts
// 5 ms, and display-only updates (0x22 = 0x0C) skip the ~82 ms analog power-up.
// A pulse is GAME_PULSE_FRAMES frames (25 ms). A pixel needs about 40 ms of
// drive to finish a transition. While a scene scrolls, pixels change again
// within a few updates, so fewer, stronger pulses keep moving scenes readable.
// Chosen by photographing Pokemon/Super Mario Land scrolling at 10/15/20/25/30
// ms pulses: 25 ms stayed at 30+ updates/s while walking and was clearest;
// 30 ms fell to 25 updates/s and disturbed emulator timing.
constexpr uint32_t GAME_SPI_HZ = 20000000;
constexpr uint8_t GAME_PULSE_FRAMES = 5;
constexpr uint8_t GAME_FRAME_RATE = 0x88;
// Pulses for each changed pixel (2 x 25 ms = 50 ms of drive). An extra white
// pulse was tried and left a dark sprite ghost, so both directions match.
constexpr uint8_t GAME_BUDGET_WHITE = 2;
constexpr uint8_t GAME_BUDGET_BLACK = 2;
constexpr uint8_t GAME_CLEAN_PULSES = 4;  // tap-to-clean: reinforce every pixel (100 ms)
constexpr size_t GAME_BYTES = GameViewport::PACKED_BYTES;
constexpr size_t GAME_WORDS = GAME_BYTES / 4;
static_assert(GAME_BYTES % 4 == 0, "whole 32-bit words");
// Once the picture has not changed for this many refreshGame calls (about a
// second), reinforce the whole window once, as tap-to-clean does.
constexpr uint16_t GAME_AUTO_CLEAN_IDLE_CALLS = 45;
static_assert(GAME_BUDGET_WHITE >= 1 && GAME_BUDGET_WHITE <= 7 && GAME_BUDGET_BLACK >= 1 &&
              GAME_BUDGET_BLACK <= 7 && GAME_CLEAN_PULSES <= 7, "3-bit pulse counters");

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
}

StickyDisplay::StickyDisplay() : GFXcanvas1(800, 480), _panel(EP397_800x480) {
    _previousBuffer = static_cast<uint8_t*>(
        heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!_previousBuffer) _previousBuffer = static_cast<uint8_t*>(malloc(FRAME_BYTES));
    if (_previousBuffer) memset(_previousBuffer, 0xFF, FRAME_BYTES);
    _pulseFrames = GAME_PULSE_FRAMES;
    _budgetWhite = GAME_BUDGET_WHITE;
    _budgetBlack = GAME_BUDGET_BLACK;
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

// --- Ink Boy fast video -----------------------------------------------------

bool StickyDisplay::allocateGame() {
    if (_gameTarget && _gameCount && _gameOld) return true;
    // Internal RAM: these are walked every update.
    auto internal = [](size_t bytes) {
        void* p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        return p ? p : heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    };
    if (!_gameTarget) _gameTarget = static_cast<uint32_t*>(internal(GAME_BYTES));
    if (!_gameCount) _gameCount = static_cast<uint32_t*>(internal(GAME_BYTES * 3));
    if (!_gameOld) _gameOld = static_cast<uint8_t*>(internal(GAME_BYTES));
    return _gameTarget && _gameCount && _gameOld;
}

void StickyDisplay::gameCommand(uint8_t command, const uint8_t* data, size_t length) {
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

void StickyDisplay::gameData(const uint8_t* data, size_t length) {
    SPI.beginTransaction(SPISettings(GAME_SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(SD_CS, HIGH);
    digitalWrite(EPD_DC, HIGH);
    digitalWrite(EPD_CS, LOW);
    SPI.writeBytes(data, length);
    digitalWrite(EPD_CS, HIGH);
    SPI.endTransaction();
}

// Sets the RAM window and address to the game window's first write row.
void StickyDisplay::gameWindow() {
    const int x0 = GameViewport::nativeX(_gameRotation), x1 = x0 + GameViewport::H - 1;
    const int y0 = GameViewport::nativeY0(_gameRotation), y1 = y0 + GameViewport::WINDOW_ROWS - 1;
    const uint8_t xRange[4] = {uint8_t(x0), uint8_t(x0 >> 8), uint8_t(x1), uint8_t(x1 >> 8)};
    const uint8_t yRange[4] = {uint8_t(y1), uint8_t(y1 >> 8), uint8_t(y0), uint8_t(y0 >> 8)};
    const uint8_t xStart[2] = {uint8_t(x0), uint8_t(x0 >> 8)};
    const uint8_t yStart[2] = {uint8_t(y1), uint8_t(y1 >> 8)};
    gameCommandByte(0x11, 0x01);  // X increment, Y decrement
    gameCommand(0x44, xRange, sizeof(xRange));
    gameCommand(0x45, yRange, sizeof(yRange));
    gameCommand(0x4E, xStart, sizeof(xStart));
    gameCommand(0x4F, yStart, sizeof(yStart));
}

void StickyDisplay::gameWaitBusy() {
    const uint32_t start = millis();
    while (digitalRead(EPD_BUSY) == HIGH) {
        if (millis() - start > 2000) {
            Serial.println("StickyDisplay: game update busy timeout");
            break;
        }
    }
}

void StickyDisplay::setGameMode(bool active) {
    if (active == _gaming) return;
    if (active) {
        if (!allocateGame()) {
            Serial.println("StickyDisplay: not enough memory for game video");
            return;
        }
        // Entered right after a full refresh: the window shows the canvas,
        // and the controller holds the OTP voltages/VCOM loaded by it. Only
        // the waveform timing is replaced.
        _gameRotation = getRotation() == 1 ? 1 : 3;
        memset(_gameTarget, 0xFF, GAME_BYTES);  // the window starts white
        memset(_gameCount, 0, GAME_BYTES * 3);
        _gameIdleCalls = 0;
        _gameAutoCleaned = false;
        uint8_t lut[105];
        buildGameLut(lut, _pulseFrames);
        // bb_epaper's full-refresh init sets Display Update Control 1 to
        // 0x40, which makes the controller read the old RAM as all zeros.
        // Per-pixel driving selects pixels through the old RAM, so restore
        // normal RAM handling for both planes.
        const uint8_t updateControl[2] = {0x00, 0x00};
        gameCommand(0x21, updateControl, sizeof(updateControl));
        gameCommandByte(0x3C, 0xC0);  // border floats during updates
        gameCommand(0x32, lut, sizeof(lut));
        _gamePowered = false;
        _gaming = true;
    } else {
        _gaming = false;
        _gameImage = nullptr;
        if (_gamePowered) {
            gameCommandByte(0x22, 0x03);  // analog + clock off
            gameCommand(0x20);
            gameWaitBusy();
            _gamePowered = false;
        }
        // bb_epaper soft-resets the controller and reloads the OTP waveform
        // on its next refresh; make that refresh a full one.
        _forceFullRefresh = true;
    }
}

bool StickyDisplay::refreshGame(bool clean, bool smooth) {
    (void)smooth;
    if (!_gaming || !_gameImage) return _gaming;
    // A picture that has stopped changing gets one automatic clean, which
    // removes residual ghosting without a visible flash.
    if (!clean && !_gameAutoCleaned && _gameIdleCalls >= GAME_AUTO_CLEAN_IDLE_CALLS) {
        clean = true;
        _gameAutoCleaned = true;
    }

    // Per-pixel drive budgets, 32 pixels per step. Each pixel has a target
    // bit (as last shown) and a 3-bit count of pulses still to deliver, kept
    // bit-sliced across three planes. A pixel is driven toward its target
    // while it has pulses left by writing old = !target, new = target; a
    // settled pixel is written old = new and receives no drive.
    const uint32_t* image = reinterpret_cast<const uint32_t*>(_gameImage);
    uint32_t* old = reinterpret_cast<uint32_t*>(_gameOld);
    uint32_t* c0 = _gameCount;
    uint32_t* c1 = _gameCount + GAME_WORDS;
    uint32_t* c2 = _gameCount + 2 * GAME_WORDS;
    // A new change uses one pulse now, so counters restart at budget - 1.
    const uint8_t whiteRestart = (clean ? GAME_CLEAN_PULSES : _budgetWhite) - 1;
    const uint8_t blackRestart = (clean ? GAME_CLEAN_PULSES : _budgetBlack) - 1;
    uint32_t anyDrive = 0, anyChange = 0;
    for (size_t i = 0; i < GAME_WORDS; ++i) {
        const uint32_t target = image[i];
        const uint32_t difference = target ^ _gameTarget[i];
        anyChange |= difference;
        const uint32_t changed = clean ? 0xFFFFFFFFu : difference;
        const uint32_t toWhite = changed & target, toBlack = changed & ~target;
        uint32_t a = c0[i], b = c1[i], c = c2[i];
        const uint32_t pending = a | b | c;
        // Decrement pending counters (bit-sliced subtract-one).
        const uint32_t borrow0 = pending;
        const uint32_t na = a ^ borrow0;
        const uint32_t borrow1 = borrow0 & ~a;
        const uint32_t nb = b ^ borrow1;
        const uint32_t borrow2 = borrow1 & ~b;
        const uint32_t nc = c ^ borrow2;
        // Changed pixels restart their budget for their direction.
        c0[i] = (na & ~changed) | ((whiteRestart & 1) ? toWhite : 0) | ((blackRestart & 1) ? toBlack : 0);
        c1[i] = (nb & ~changed) | ((whiteRestart & 2) ? toWhite : 0) | ((blackRestart & 2) ? toBlack : 0);
        c2[i] = (nc & ~changed) | ((whiteRestart & 4) ? toWhite : 0) | ((blackRestart & 4) ? toBlack : 0);
        const uint32_t drive = changed | pending;
        old[i] = target ^ drive;
        _gameTarget[i] = target;
        anyDrive |= drive;
    }
    if (anyChange) {
        _gameIdleCalls = 0;
        _gameAutoCleaned = false;
    } else if (_gameIdleCalls < GAME_AUTO_CLEAN_IDLE_CALLS) {
        ++_gameIdleCalls;
    }
    if (!anyDrive) return true;  // settled and unchanged: nothing to drive

    gameWindow();
    gameCommand(0x26, _gameOld, GAME_BYTES);       // old plane
    gameWindow();
    gameCommand(0x24, _gameImage, GAME_BYTES);     // new plane
    gameCommandByte(0x22, _gamePowered ? 0x0C : 0xCC);
    gameCommand(0x20);
    gameWaitBusy();
    _gamePowered = true;
    return true;
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
