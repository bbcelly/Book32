#include "AppSudoku.h"
#include "SoundMgr.h"
#include "DisplayMgr.h"
#include "InputMgr.h"
#include "FontMgr.h"
#include "AppMgr.h"
#include "icon_sudoku.h"
#include "../Book32_Core/Book32FS.h"
#include <ArduinoJson.h>
#include <esp_random.h>

using Sudoku::Difficulty;

const uint8_t* AppSudoku::getIconImage() { return icon_sudoku_160x160; }

static constexpr const char* SAVE_PATH = "/sudoku.json";
// Partial refreshes leave ghosts on e-paper; clean up with a full one now and then.
static constexpr int PARTIALS_PER_FULL = 40;
static constexpr int MARGIN = 10;
static constexpr int HEADER_H = 60;

// Digit height of the FreeSans sizes FontMgr maps to, for vertical centering.
static int capHeight(int fontSize) {
    switch (fontSize) {
        case FONT_SIZE_HEADER: return 34;
        case FONT_SIZE_TITLE:
        case FONT_SIZE_SUBTITLE: return 25;
        case FONT_SIZE_MENU:
        case FONT_SIZE_BODY: return 17;
        default: return 13;
    }
}

static void drawCentered(Book32Display& display, const char* text, const AppSudoku::Rect& r, int fontSize,
                         uint16_t color, bool bold = false) {
    FontMgr& fontMgr = FontMgr::getInstance();
    const int x = r.x + (r.w - fontMgr.getTextWidth(text, fontSize)) / 2;
    const int y = r.y + (r.h + capHeight(fontSize)) / 2;
    fontMgr.drawText(display, text, x, y, fontSize, color);
    if (bold) fontMgr.drawText(display, text, x + 1, y, fontSize, color);
}

static void formatTime(char* out, size_t size, uint32_t ms) {
    const uint32_t seconds = ms / 1000;
    snprintf(out, size, "%lu:%02lu", static_cast<unsigned long>(seconds / 60),
             static_cast<unsigned long>(seconds % 60));
}

AppSudoku::Layout AppSudoku::layout() const {
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    const int w = display.width(), h = display.height();
    Layout l;
    l.header = {0, 0, w, HEADER_H + 2};
    l.back = {0, 0, 170, HEADER_H};
    l.cell = min((w - 2 * MARGIN) / 9, (h - 380) / 9);
    const int g = l.cell * 9;
    const int gx = (w - g) / 2;
    l.grid = {gx, HEADER_H + 12, g, g};

    const int gap = 8, keyH = 68;
    const int keyW = (g - 4 * gap) / 5;
    const int keysY = l.grid.y + g + 18;
    for (int i = 0; i < 10; ++i) {
        const int key = i < 9 ? i + 1 : KEY_ERASE;
        l.keys[key] = {gx + (i % 5) * (keyW + gap), keysY + (i / 5) * (keyH + gap), keyW, keyH};
    }
    l.keys[0] = {0, 0, 0, 0};
    const int actionW = (g - 2 * gap) / 3;
    const int actionY = keysY + 2 * (keyH + gap) + 8;
    for (int i = 0; i < 3; ++i) l.keys[KEY_NOTES + i] = {gx + i * (actionW + gap), actionY, actionW, 62};

    const int choiceH = 100, choiceGap = 20, choicesY = HEADER_H + 110;
    for (int d = 0; d < Sudoku::DIFFICULTY_COUNT; ++d)
        l.choices[d] = {gx, choicesY + d * (choiceH + choiceGap), g, choiceH};
    l.cancel = {gx, choicesY + Sudoku::DIFFICULTY_COUNT * (choiceH + choiceGap) + 10, g, 80};
    return l;
}

void AppSudoku::start() {
    loadGame();
    _view = _hasGame ? VIEW_PLAYING : VIEW_CHOOSING;
    if (_view == VIEW_PLAYING) startClock();
    _partialCount = 0;
    requestRedraw(true);
    InputMgr::getInstance().setCallback(std::bind(&AppSudoku::handleInput, this, std::placeholders::_1));
    InputMgr::getInstance().setTouchCallback(std::bind(&AppSudoku::handleTouch, this,
                                                       std::placeholders::_1, std::placeholders::_2));
}

void AppSudoku::stop() {
    stopClock();
    if (_hasGame) saveGame();
    InputMgr::getInstance().clearCallback();
    InputMgr::getInstance().clearTouchCallback();
}

void AppSudoku::startClock() {
    if (_hasGame && !_solved && !_runningSince) _runningSince = millis() | 1;
}

void AppSudoku::stopClock() {
    if (!_runningSince) return;
    _elapsedMs += millis() - _runningSince;
    _runningSince = 0;
}

uint32_t AppSudoku::elapsedMs() const {
    return _elapsedMs + (_runningSince ? millis() - _runningSince : 0);
}

void AppSudoku::newGame(Difficulty difficulty) {
    uint8_t puzzle[81], solution[81];
    Sudoku::Rng rng(esp_random());
    const unsigned long t0 = millis();
    Sudoku::generate(difficulty, rng, puzzle, solution);
    Serial.printf("Sudoku: %s puzzle in %lu ms\n", Sudoku::difficultyName(difficulty), millis() - t0);
    _game.start(puzzle, difficulty);
    _hasGame = true;
    _solved = false;
    _notesMode = false;
    _selected = -1;
    _elapsedMs = 0;
    _runningSince = 0;
    _view = VIEW_PLAYING;
    startClock();
    saveGame();
    requestRedraw(true);
}

void AppSudoku::loadGame() {
    _hasGame = false;
    _solved = false;
    _selected = -1;
    _notesMode = false;
    _elapsedMs = 0;
    _runningSince = 0;
    if (!EbookFS.exists(SAVE_PATH)) return;
    File file = EbookFS.open(SAVE_PATH, "r");
    if (!file) return;
    DynamicJsonDocument doc(1536);
    const DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error || (doc["v"] | 0) != 1) return;
    const char* given = doc["given"] | "";
    const char* value = doc["value"] | "";
    const char* notes = doc["notes"] | "";
    if (strlen(given) != 81 || strlen(value) != 81 || strlen(notes) != 243) return;

    uint8_t g[81], v[81];
    uint16_t n[81];
    for (int i = 0; i < 81; ++i) {
        g[i] = given[i] == '1';
        v[i] = (value[i] >= '1' && value[i] <= '9') ? value[i] - '0' : 0;
        char hex[4] = {notes[i * 3], notes[i * 3 + 1], notes[i * 3 + 2], 0};
        n[i] = static_cast<uint16_t>(strtoul(hex, nullptr, 16));
    }
    _game.restore(g, v, n, static_cast<Difficulty>(doc["difficulty"] | 0));
    _elapsedMs = (doc["elapsed"] | 0UL) * 1000UL;
    _solved = _game.solved();
    _hasGame = true;
}

void AppSudoku::saveGame() {
    char given[82], value[82], notes[244];
    for (int i = 0; i < 81; ++i) {
        given[i] = _game.isGiven(i) ? '1' : '0';
        value[i] = '0' + _game.value(i);
        snprintf(notes + i * 3, 4, "%03x", _game.notes(i));
    }
    given[81] = value[81] = 0;
    DynamicJsonDocument doc(1024);
    doc["v"] = 1;
    doc["difficulty"] = static_cast<int>(_game.difficulty());
    doc["given"] = given;
    doc["value"] = value;
    doc["notes"] = notes;
    doc["elapsed"] = elapsedMs() / 1000;
    File file = EbookFS.open(SAVE_PATH, FILE_WRITE);
    if (!file) {
        Serial.println("Sudoku: could not save the game");
        return;
    }
    serializeJson(doc, file);
    file.close();
}

void AppSudoku::requestRedraw(bool full) {
    if (full) _fullRefresh = true;
    _headerOnly = false;
    _needsRedraw = true;
}

void AppSudoku::forceRedraw() { requestRedraw(true); }

void AppSudoku::afterMove() {
    if (!_solved && _game.solved()) {
        _solved = true;
        stopClock();
        _selected = -1;
        SoundMgr::getInstance().beep();
        requestRedraw(true);
    } else {
        requestRedraw();
    }
    saveGame();
}

void AppSudoku::pressKey(int key) {
    if (key == KEY_NEW) {
        stopClock();
        _view = VIEW_CHOOSING;
        requestRedraw(true);
        return;
    }
    if (key == KEY_NOTES) {
        _notesMode = !_notesMode;
        requestRedraw();
        return;
    }
    if (_solved) return;
    bool changed = false;
    if (key == KEY_UNDO) changed = _game.undo();
    else if (_selected < 0) return;
    else if (key == KEY_ERASE) changed = _game.clear(_selected);
    else if (key >= 1 && key <= 9)
        changed = _notesMode ? _game.toggleNote(_selected, key) : _game.setValue(_selected, key);
    if (changed) afterMove();
}

// The BOOT key steps through the cells still to fill.
void AppSudoku::moveSelection(int step) {
    if (_view != VIEW_PLAYING || _solved) return;
    int cell = _selected;
    for (int i = 0; i < 81; ++i) {
        cell = (cell + step + 81) % 81;
        if (!_game.isGiven(cell) && !_game.value(cell)) {
            _selected = cell;
            requestRedraw();
            return;
        }
    }
}

void AppSudoku::handleInput(InputAction action) {
    if (action == INPUT_NONE) return;
    if (action == INPUT_BACK) {
        if (_view == VIEW_CHOOSING && _hasGame) {
            _view = VIEW_PLAYING;
            startClock();
            requestRedraw(true);
        } else {
            AppMgr::getInstance().switchTo(0);
        }
        return;
    }
    if (action == INPUT_NEXT) moveSelection(1);
    else if (action == INPUT_PREV) moveSelection(-1);
}

void AppSudoku::handleTouch(uint16_t x, uint16_t y) {
    const Layout l = layout();
    if (_view == VIEW_CHOOSING) {
        for (int d = 0; d < Sudoku::DIFFICULTY_COUNT; ++d) {
            if (l.choices[d].contains(x, y)) {
                SoundMgr::getInstance().beep();
                newGame(static_cast<Difficulty>(d));
                return;
            }
        }
        if (l.back.contains(x, y) || (_hasGame && l.cancel.contains(x, y))) {
            SoundMgr::getInstance().beep();
            handleInput(INPUT_BACK);
        }
        return;
    }

    if (l.back.contains(x, y)) {
        SoundMgr::getInstance().beep();
        AppMgr::getInstance().switchTo(0);
        return;
    }
    if (l.grid.contains(x, y)) {
        if (_solved) return;
        const int cell = ((y - l.grid.y) / l.cell) * 9 + (x - l.grid.x) / l.cell;
        if (cell != _selected) {
            _selected = cell;
            requestRedraw();
        }
        return;
    }
    for (int key = 1; key < KEY_COUNT; ++key) {
        if (l.keys[key].contains(x, y)) {
            SoundMgr::getInstance().beep();
            pressKey(key);
            return;
        }
    }
}

void AppSudoku::update() {
    // The header shows whole minutes, so the panel changes once a minute at most.
    if (_view == VIEW_PLAYING && _runningSince && !_needsRedraw &&
        static_cast<int>(elapsedMs() / 60000) != _shownMinutes) {
        _headerOnly = true;
        _needsRedraw = true;
    }
}

void AppSudoku::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;
    if (_view == VIEW_CHOOSING) drawChoosing();
    else drawPlaying();
}

void AppSudoku::drawHeader(Book32Display& display, const Layout& l) {
    FontMgr& fontMgr = FontMgr::getInstance();
    fontMgr.drawText(display, "<  Back", 15, 38, FONT_SIZE_MENU, GxEPD_BLACK);
    if (_view == VIEW_PLAYING && _hasGame) {
        fontMgr.drawTextCentered(display, Sudoku::difficultyName(_game.difficulty()), 38, FONT_SIZE_MENU,
                                 GxEPD_BLACK);
        char time[24];
        const uint32_t ms = elapsedMs();
        _shownMinutes = ms / 60000;
        if (_solved) formatTime(time, sizeof(time), ms);
        else snprintf(time, sizeof(time), "%d min", _shownMinutes);
        fontMgr.drawTextRight(display, time, l.header.w - 15, 38, FONT_SIZE_MENU, GxEPD_BLACK);
    } else {
        fontMgr.drawTextCentered(display, "Sudoku", 38, FONT_SIZE_MENU, GxEPD_BLACK);
    }
    display.drawLine(0, HEADER_H, l.header.w, HEADER_H, GxEPD_BLACK);
}

void AppSudoku::drawCell(Book32Display& display, const Layout& l, int cell) {
    const Rect r = {l.grid.x + (cell % 9) * l.cell, l.grid.y + (cell / 9) * l.cell, l.cell, l.cell};
    const bool selected = cell == _selected;
    const uint16_t ink = selected ? GxEPD_WHITE : GxEPD_BLACK;
    if (selected) display.fillRect(r.x + 2, r.y + 2, r.w - 3, r.h - 3, GxEPD_BLACK);

    const uint8_t digit = _game.value(cell);
    if (digit) {
        const char text[2] = {static_cast<char>('0' + digit), 0};
        const bool given = _game.isGiven(cell);
        drawCentered(display, text, r, given ? FONT_SIZE_HEADER : FONT_SIZE_TITLE, ink, given);
        if (_game.conflicts(cell)) display.fillRect(r.x + r.w / 4, r.y + r.h - 9, r.w / 2, 3, ink);
        return;
    }
    const uint16_t notes = _game.notes(cell);
    if (!notes) return;
    const int sub = r.w / 3;
    for (int d = 1; d <= 9; ++d) {
        if (!(notes & (1u << d))) continue;
        const char text[2] = {static_cast<char>('0' + d), 0};
        const Rect s = {r.x + ((d - 1) % 3) * sub, r.y + ((d - 1) / 3) * sub, sub, sub};
        drawCentered(display, text, s, FONT_SIZE_SMALL, ink);
    }
}

static void drawButton(Book32Display& display, const AppSudoku::Rect& r, const char* label, int fontSize,
                       bool filled) {
    if (filled) {
        display.fillRect(r.x, r.y, r.w, r.h, GxEPD_BLACK);
    } else {
        display.drawRect(r.x, r.y, r.w, r.h, GxEPD_BLACK);
        display.drawRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, GxEPD_BLACK);
    }
    drawCentered(display, label, r, fontSize, filled ? GxEPD_WHITE : GxEPD_BLACK);
}

void AppSudoku::drawPlaying() {
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();
    const Layout l = layout();

    bool headerOnly = false;
    if (_fullRefresh || ++_partialCount >= PARTIALS_PER_FULL) {
        display.setFullWindow();
        _partialCount = 0;
    } else if (_headerOnly) {
        display.setPartialWindow(l.header.x, l.header.y, l.header.w, l.header.h);
        headerOnly = true;
    } else {
        display.setPartialWindow(0, 0, display.width(), display.height());
    }
    _fullRefresh = false;
    _headerOnly = false;

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        drawHeader(display, l);
        if (headerOnly) continue;

        for (int cell = 0; cell < 81; ++cell) drawCell(display, l, cell);
        for (int k = 0; k <= 9; ++k) {
            const int t = (k % 3 == 0) ? 3 : 1;
            const int p = k * l.cell - (t > 1 ? 1 : 0);
            display.fillRect(l.grid.x + p, l.grid.y - 1, t, l.grid.h + 3, GxEPD_BLACK);
            display.fillRect(l.grid.x - 1, l.grid.y + p, l.grid.w + 3, t, GxEPD_BLACK);
        }

        for (int d = 1; d <= 9; ++d) {
            const char text[2] = {static_cast<char>('0' + d), 0};
            drawButton(display, l.keys[d], text, FONT_SIZE_TITLE, false);
        }
        drawButton(display, l.keys[KEY_ERASE], "Del", FONT_SIZE_MENU, false);
        drawButton(display, l.keys[KEY_NOTES], "Notes", FONT_SIZE_MENU, _notesMode);
        drawButton(display, l.keys[KEY_UNDO], "Undo", FONT_SIZE_MENU, false);
        drawButton(display, l.keys[KEY_NEW], "New game", FONT_SIZE_MENU, false);

        const Rect& last = l.keys[KEY_NEW];
        const int statusY = last.y + last.h + 40;
        if (_solved) {
            char text[48], time[16];
            formatTime(time, sizeof(time), elapsedMs());
            snprintf(text, sizeof(text), "Solved in %s!", time);
            fontMgr.drawTextCentered(display, text, statusY, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        } else if (_notesMode) {
            fontMgr.drawTextCentered(display, "Notes on: digits are pencil marks", statusY, FONT_SIZE_SMALL,
                                     GxEPD_BLACK);
        }
    } while (display.nextPage());
}

void AppSudoku::drawChoosing() {
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();
    const Layout l = layout();
    display.setFullWindow();
    _fullRefresh = false;
    _headerOnly = false;
    _partialCount = 0;

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        drawHeader(display, l);
        fontMgr.drawTextCentered(display, "New game", HEADER_H + 70, FONT_SIZE_TITLE, GxEPD_BLACK);
        for (int d = 0; d < Sudoku::DIFFICULTY_COUNT; ++d) {
            const Difficulty difficulty = static_cast<Difficulty>(d);
            const Rect& r = l.choices[d];
            drawButton(display, {r.x, r.y, r.w, r.h - 30}, Sudoku::difficultyName(difficulty), FONT_SIZE_SUBTITLE,
                       false);
            char clues[24];
            snprintf(clues, sizeof(clues), "%d+ clues", Sudoku::targetClues(difficulty));
            fontMgr.drawTextCentered(display, clues, r.y + r.h - 6, FONT_SIZE_SMALL, GxEPD_BLACK);
        }
        if (_hasGame) drawButton(display, l.cancel, "Back to the game", FONT_SIZE_MENU, false);
    } while (display.nextPage());
}
