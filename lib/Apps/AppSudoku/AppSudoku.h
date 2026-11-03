#pragma once
#include "BaseApp.h"
#include "../Book32_Core/DisplayMgr.h"
#include "../Book32_Core/InputMgr.h"
#include "SudokuEngine.h"
#include <Arduino.h>

// Classic 9x9 Sudoku for the touch boards. Puzzles are generated on the
// device with one solution each; the game in progress is saved after every
// move to /sudoku.json, so it survives leaving the app and deep sleep.
class AppSudoku : public App {
public:
    const char* getName() override { return "Sudoku"; }
    const uint8_t* getIconImage() override;

    void start() override;
    void stop() override;
    void update() override;
    void draw() override;
    void forceRedraw() override;

    void handleInput(InputAction action);
    void handleTouch(uint16_t x, uint16_t y);

    struct Rect {
        int x, y, w, h;
        bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    };

private:
    enum View { VIEW_PLAYING, VIEW_CHOOSING };
    enum Key { KEY_ERASE = 10, KEY_NOTES, KEY_UNDO, KEY_NEW, KEY_COUNT };

    Sudoku::Game _game;
    View _view = VIEW_PLAYING;
    bool _hasGame = false;
    bool _solved = false;
    bool _notesMode = false;
    int _selected = -1;
    uint32_t _elapsedMs = 0;        // play time before _runningSince
    unsigned long _runningSince = 0;  // 0 while the clock is stopped
    int _shownMinutes = -1;

    bool _needsRedraw = false;
    bool _fullRefresh = true;
    bool _headerOnly = false;
    int _partialCount = 0;

    // Layout for the current screen size, shared by drawing and touch.
    struct Layout {
        Rect back, header, grid;
        int cell;
        Rect keys[KEY_COUNT];       // 1..9 digits, then the action keys
        Rect choices[Sudoku::DIFFICULTY_COUNT];
        Rect cancel;
    };
    Layout layout() const;

    void newGame(Sudoku::Difficulty difficulty);
    void pressKey(int key);
    void afterMove();
    void moveSelection(int step);
    void startClock();
    void stopClock();
    uint32_t elapsedMs() const;
    void loadGame();
    void saveGame();
    void requestRedraw(bool full = false);

    void drawPlaying();
    void drawChoosing();
    void drawHeader(Book32Display& display, const Layout& l);
    void drawCell(Book32Display& display, const Layout& l, int cell);
};
