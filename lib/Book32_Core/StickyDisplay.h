#pragma once

#if defined(BOARD_SEEED_STICKY)

#include <Adafruit_GFX.h>
#include <bb_epaper.h>
#include <SPI.h>

// GFX-compatible adapter for the Sticky's native-landscape SSD1677 panel.
// InkDeck keeps one 800x480 framebuffer, so existing GFX font and bitmap code
// can run unchanged while bb_epaper handles the panel-specific waveform.
class StickyDisplay : public GFXcanvas1 {
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

    // Ink Boy fast video. The game window is driven directly with a short
    // register waveform and per-pixel drive budgets; the rest of the screen
    // is left untouched. Same interface as LilygoDisplay.
    void setGameMode(bool active);
    void setNesGame(bool) {}
    // Borrowed packed frame (GameViewport layout); caller keeps it alive
    // until the next setGameFrame or setGameMode(false).
    void setGameFrame(const uint8_t* packed) { _gameImage = packed; }
    bool refreshGame(bool clean, bool smooth);

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
    const uint8_t* _gameImage = nullptr;
    uint32_t* _gameTarget = nullptr;  // target shown by the last update
    uint32_t* _gameCount = nullptr;   // 3 bit-planes of remaining pulses
    uint8_t* _gameOld = nullptr;      // "old" RAM plane for the next update
    uint16_t _gameIdleCalls = 0;      // refreshes since the picture last changed
    bool _gameAutoCleaned = false;    // one automatic clean per still picture
    uint8_t _pulseFrames = 0;         // waveform frames per pulse (5 ms each)
    uint8_t _budgetWhite = 0;         // pulses for a pixel turning white
    uint8_t _budgetBlack = 0;         // pulses for a pixel turning black
    bool allocateGame();
    void gameCommand(uint8_t command, const uint8_t* data = nullptr, size_t length = 0);
    void gameCommandByte(uint8_t command, uint8_t value) { gameCommand(command, &value, 1); }
    void gameData(const uint8_t* data, size_t length);
    void gameWindow();
    void gameWaitBusy();

    void flush();
};

#endif
