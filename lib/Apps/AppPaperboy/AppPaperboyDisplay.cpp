// Inkade on the Sticky: InkBoy's display loop, display settings, presets and
// the transition test.
#include "AppPaperboy.h"
#if INKADE_INKBOY_VIDEO
#include "DisplayMgr.h"
#include "GameViewport.h"
#include "InkBoyDrive.h"
#include <esp_timer.h>

namespace {
using namespace InkBoyDrive;

// 480x800 portrait. Rows are steppers ("-  value  +") or actions.
constexpr int LEFT = 20, WIDTH = 440, ROW_Y = 124, ROW_H = 50, ROW_STEP = 54, MAX_ROWS = 12;
constexpr int MINUS_X = 280, PLUS_X = 416, BUTTON_W = 44;
constexpr int MESSAGE_Y = 784;
static_assert(ROW_Y + MAX_ROWS * ROW_STEP <= MESSAGE_Y, "rows fit above the message line");

// Row ids. Doses, holds, routes and preset files add an index to their base.
enum : int {
    R_MODE, R_SHOW_FPS, R_PRESET, R_BLACK, R_WHITE, R_RETARGET, R_SCANS, R_PAIR, R_HISTORY,
    R_DOSES, R_HOLDS, R_ROUTES, R_TEST, R_RESET,
    R_DOSE = 100,          // + transition index 0..11
    R_HOLD = 150,          // + transition index 0..11
    R_RETURN = 200,        // + route
    R_REVERSE = 300,       // + route
    R_PRESET_DEFAULT = 400, R_PRESET_SAVE_NEW, R_PRESET_SAVE,
    R_PRESET_FILE = 500,   // + file index
};
constexpr char SHADE[4] = {'K', 'D', 'L', 'W'};  // InkBoy order
constexpr const char* SHADE_NAMES[4] = {"Black", "Dark", "Light", "White"};
constexpr const char* HISTORY_LABELS[4] = {"Off", "Returns", "Reversals", "Both"};
constexpr const char* ROUTE_LABELS[ROUTES] = {"L>K>L", "D>K>D", "D>W>D", "L>W>L", "D>L>D", "L>D>L"};
constexpr unsigned MAX_PRESET_ROWS = 9;

// Transition `t` (0..11) as (from, to), grouped by starting shade.
void transition(int t, int& from, int& to) {
    from = t / 3;
    const int k = t % 3;
    to = k < from ? k : k + 1;
}

void text(Book32Display& d, const String& s, int x, int y, int size = 2) {
    d.setFont(nullptr); d.setTextSize(size); d.setTextWrap(false); d.setTextColor(GxEPD_BLACK);
    d.setCursor(x, y); d.print(s); d.setTextSize(1);
}
void centered(Book32Display& d, const String& s, int x, int w, int y, int size = 2) {
    text(d, s, x + (w - int(s.length()) * 6 * size) / 2, y, size);
}
template <typename T> T step(T value, int delta, T lo, T hi, T by = 1) {
    const int v = int(value) + delta * int(by);
    return T(v < lo ? lo : v > hi ? hi : v);
}
}

// --- InkBoy's display loop ---------------------------------------------------

// The newest complete emulator frame becomes the displayed snapshot.
bool AppPaperboy::latestImage() {
    bool changed = false;
    if (_frameReady.load()) {
        xSemaphoreTake(_frameMutex, portMAX_DELAY);
        if (_frameReady.exchange(false)) {
            changed = memcmp(_snapshot, _latest, packedBytes()) != 0;
            std::swap(_snapshot, _latest);
            _snapshotSequence = _latestSequence;
            _snapshotValid = true;
        }
        xSemaphoreGive(_frameMutex);
    }
    return changed;
}

// game_session_play: sample the newest image on a 33.3 ms clock; drive only
// for a new image or remaining drive; switch the analog supply off after
// 750 ms without either.
void AppPaperboy::drawStickyGame() {
    auto& d = DisplayMgr::getInstance().getDisplay();
    if (_redraw) {
        d.setGameMode(false);
        paintGame(); d.refresh(false);
        d.setGameMode(true);
        _redraw = false; _forceClean = true;
    }
    d.gameWaitSlot(_nextImage);
    const int64_t now = esp_timer_get_time();
    bool changed = false;
    if (now >= _nextImage) {
        changed = latestImage() || _forceClean;  // the first image after a full refresh
        _forceClean = false;
        _nextImage = InkBoyDrive::nextImage(_nextImage, now);
        requestNextFrame(false);
    }
    d.setGameFrame(_snapshot);
    if (changed) _lastChange = now;
    if (changed || d.gamePending()) {
        const uint32_t started = micros();
        if (!d.refreshGame()) Serial.println("Inkade: display update was not delivered");
        _refreshUs += micros() - started; ++_regularUpdates; ++_updates;
        _analogOn = true;
        _presentations.observe(_snapshotSequence, _snapshotValid, false);
        countFps(changed);
    }
    if (!changed && !d.gamePending() && _analogOn && now - _lastChange >= InkBoyDrive::IDLE_POWER_OFF_US) {
        d.gamePowerOff();
        _analogOn = false;
    }
}

// --- Rows ----------------------------------------------------------------------

std::vector<int> AppPaperboy::settingsRows() const {
    std::vector<int> rows;
    const bool gray = _display.video.mode == MODE_GRAYSCALE;
    switch (_settingsPage) {
    case PAGE_MAIN:
        rows = {R_MODE, R_SHOW_FPS, R_PRESET};
        if (gray) rows.insert(rows.end(), {R_RETARGET, R_SCANS, R_PAIR, R_HISTORY, R_DOSES, R_HOLDS, R_ROUTES});
        else rows.insert(rows.end(), {R_BLACK, R_WHITE});
        rows.insert(rows.end(), {R_TEST, R_RESET});
        break;
    case PAGE_DOSES:
        for (int t = 0; t < 12; ++t) rows.push_back(R_DOSE + t);
        break;
    case PAGE_HOLDS:
        for (int t = 0; t < 12; ++t) rows.push_back(R_HOLD + t);
        break;
    case PAGE_ROUTES:
        for (int r = 0; r < ROUTES; ++r) { rows.push_back(R_RETURN + r); rows.push_back(R_REVERSE + r); }
        break;
    case PAGE_PRESETS:
        rows.push_back(R_PRESET_DEFAULT);
        for (unsigned i = 0; i < _presets.size() && i < MAX_PRESET_ROWS; ++i) rows.push_back(R_PRESET_FILE + i);
        rows.push_back(R_PRESET_SAVE_NEW);
        if (_display.modified[_display.video.mode] && _display.preset[_display.video.mode] != "Default")
            rows.push_back(R_PRESET_SAVE);
        break;
    }
    return rows;
}

bool AppPaperboy::rowIsAction(int id) const {
    return id == R_PRESET || id == R_DOSES || id == R_HOLDS || id == R_ROUTES || id == R_TEST || id == R_RESET ||
           id >= R_PRESET_DEFAULT;
}

String AppPaperboy::rowLabel(int id) const {
    const uint8_t mode = _display.video.mode;
    switch (id) {
    case R_MODE: return "Mode";
    case R_SHOW_FPS: return "Show FPS";
    case R_PRESET: return "Preset: " + _display.preset[mode] + (_display.modified[mode] ? "*" : "");
    case R_BLACK: return "Black pulses";
    case R_WHITE: return "White pulses";
    case R_RETARGET: return "Retarget dose";
    case R_SCANS: return "Scans/frame";
    case R_PAIR: return "Pair hold mode";
    case R_HISTORY: return "History";
    case R_DOSES: return "Doses";
    case R_HOLDS: return "Hold flags";
    case R_ROUTES: return "History / selective";
    case R_TEST: return "Transition test";
    case R_RESET: return "Restore default";
    case R_PRESET_DEFAULT: return "Load Default";
    case R_PRESET_SAVE_NEW: return "Save as new";
    case R_PRESET_SAVE: return "Save over " + _display.preset[mode];
    }
    if (id >= R_PRESET_FILE) return "Load " + _presets[id - R_PRESET_FILE].name;
    if (id >= R_REVERSE) return String(ROUTE_LABELS[id - R_REVERSE]) + " reverse";
    if (id >= R_RETURN) return String(ROUTE_LABELS[id - R_RETURN]) + " completed";
    int from, to; transition((id >= R_HOLD ? id - R_HOLD : id - R_DOSE), from, to);
    return String(SHADE_NAMES[from]) + " > " + SHADE_NAMES[to];
}

String AppPaperboy::rowValue(int id) const {
    const Settings& v = _display.video;
    switch (id) {
    case R_MODE: return v.mode == MODE_GRAYSCALE ? "Gray" : "Dither";
    case R_SHOW_FPS: return v.showFps ? "On" : "Off";
    case R_BLACK: return String(v.blackPulses);
    case R_WHITE: return String(v.whitePulses);
    case R_RETARGET: return String(retargetPercent(v.gray)) + "%";
    case R_SCANS: return v.immediate ? String("Pair") : v.gray.scanPolicy ? String("1") : String("Auto");
    case R_PAIR: return v.immediate ? "On" : "Off";
    case R_HISTORY: return HISTORY_LABELS[v.adaptive.historyMode & 3];
    }
    if (id >= R_PRESET_DEFAULT) return "";
    if (id >= R_REVERSE) {
        const uint8_t p = v.adaptive.reversePercent[id - R_REVERSE];
        return p ? String(p) + "%" : String("Base");
    }
    if (id >= R_RETURN) {
        const int r = v.adaptive.returnDelta[id - R_RETURN];
        return r > 0 ? "+" + String(r) : String(r);
    }
    int from, to;
    if (id >= R_HOLD) {
        transition(id - R_HOLD, from, to);
        return v.holds & (1u << (from * 4 + to)) ? "On" : "Off";
    }
    transition(id - R_DOSE, from, to);
    return String(v.gray.dose[from][to]);
}

void AppPaperboy::changeRow(int id, int delta) {
    Settings& v = _display.video;
    const uint8_t mode = v.mode;
    switch (id) {
    case R_MODE:
        v.mode = v.mode == MODE_GRAYSCALE ? MODE_DITHERED : MODE_GRAYSCALE;
        settingsChanged(false);
        return;
    case R_SHOW_FPS: v.showFps = !v.showFps; settingsChanged(false); return;
    case R_PRESET:
        _presets = InkadeSettings::list(mode);
        _settingsPage = PAGE_PRESETS; _redraw = true;
        return;
    case R_BLACK: v.blackPulses = step<uint8_t>(v.blackPulses, delta, 1, DITHER_PULSES_MAX); break;
    case R_WHITE: v.whitePulses = step<uint8_t>(v.whitePulses, delta, 1, DITHER_PULSES_MAX); break;
    case R_RETARGET: v.gray.retargetGain = step<uint8_t>(v.gray.retargetGain, delta, 0, 4); break;
    case R_SCANS: v.gray.scanPolicy = !v.gray.scanPolicy; break;  // no effect in pair hold mode
    case R_PAIR: v.immediate = !v.immediate; break;
    case R_HISTORY: v.adaptive.historyMode = (v.adaptive.historyMode + (delta < 0 ? 3 : 1)) & 3; break;
    case R_DOSES: _settingsPage = PAGE_DOSES; _redraw = true; return;
    case R_HOLDS: _settingsPage = PAGE_HOLDS; _redraw = true; return;
    case R_ROUTES: _settingsPage = PAGE_ROUTES; _redraw = true; return;
    case R_TEST: startTest(); return;
    case R_RESET:
    case R_PRESET_DEFAULT: {
        const bool fps = v.showFps;
        if (mode == MODE_GRAYSCALE) {
            v.gray = DEFAULT_SETTINGS.gray; v.holds = DEFAULT_SETTINGS.holds;
            v.immediate = DEFAULT_SETTINGS.immediate; v.adaptive = DEFAULT_SETTINGS.adaptive;
        } else {
            v.blackPulses = DEFAULT_SETTINGS.blackPulses; v.whitePulses = DEFAULT_SETTINGS.whitePulses;
        }
        v.showFps = fps;
        _display.preset[mode] = "Default"; _display.modified[mode] = false;
        _settingsMessage = id == R_RESET ? "Restored the default settings" : "Loaded Default";
        _settingsPage = PAGE_MAIN;
        settingsChanged(false);
        return;
    }
    case R_PRESET_SAVE_NEW: {
        String name, path;
        if (InkadeSettings::saveNewPreset(v, name, path)) {
            _display.preset[mode] = name; _display.modified[mode] = false;
            _settingsMessage = "Saved " + name;
        } else _settingsMessage = "Could not save; check the MicroSD card";
        _presets = InkadeSettings::list(mode);
        settingsChanged(false);
        return;
    }
    case R_PRESET_SAVE: {
        const String path = InkadeSettings::findPreset(mode, _display.preset[mode]);
        if (!path.isEmpty() && InkadeSettings::overwritePreset(path, v, _display.preset[mode])) {
            _display.modified[mode] = false;
            _settingsMessage = "Saved " + _display.preset[mode] + " (old copy kept as .bak)";
        } else _settingsMessage = "Could not save; check the MicroSD card";
        settingsChanged(false);
        return;
    }
    default: break;
    }
    if (id >= R_PRESET_FILE) {
        const InkadeSettings::PresetFile& file = _presets[id - R_PRESET_FILE];
        String name;
        if (InkadeSettings::loadPreset(file.path, mode, v, name)) {
            _display.preset[mode] = name; _display.modified[mode] = false;
            _settingsMessage = "Loaded " + name;
            _settingsPage = PAGE_MAIN;
        } else _settingsMessage = "That preset could not be read";
        settingsChanged(false);
        return;
    }
    if (id >= R_REVERSE) {
        uint8_t& p = v.adaptive.reversePercent[id - R_REVERSE];
        const int next = (p ? p : 25) + delta * 25;  // Base sits below 50%
        p = next < 50 ? 0 : uint8_t(next > 200 ? 200 : next);
    } else if (id >= R_RETURN) {
        int8_t& r = v.adaptive.returnDelta[id - R_RETURN];
        r = step<int8_t>(r, delta, -3, 3);
    } else if (id >= R_HOLD) {
        int from, to; transition(id - R_HOLD, from, to);
        v.holds ^= uint16_t(1u << (from * 4 + to));
    } else if (id >= R_DOSE) {
        int from, to; transition(id - R_DOSE, from, to);
        v.gray.dose[from][to] = step<uint8_t>(v.gray.dose[from][to], delta, 1, GRAY_DOSE_MAX);
    }
    settingsChanged(true);
}

void AppPaperboy::settingsChanged(bool modeSetting) {
    InkBoyDrive::sanitize(_display.video);
    if (modeSetting) {
        _display.modified[_display.video.mode] = true;
        _settingsMessage = "";
    }
    if (!InkadeSettings::save(_display)) _settingsMessage = "Settings not saved; check the MicroSD card";
    DisplayMgr::getInstance().getDisplay().setGameSettings(_display.video);
    _redraw = true;
}

// --- Settings screen -------------------------------------------------------------

void AppPaperboy::openSettings() {
    _screen = SCREEN_SETTINGS;
    _settingsPage = PAGE_MAIN;
    _settingsMessage = "";
    _redraw = true;
}

void AppPaperboy::paintSettings() {
    auto& d = DisplayMgr::getInstance().getDisplay();
    d.setGameMode(false);
    d.setFullWindow(); d.fillScreen(GxEPD_WHITE);
    const char* titles[] = {"Display", "Doses", "Hold flags", "History", "Presets"};
    text(d, _settingsPage == PAGE_MAIN ? "< Inkade" : "< Display", LEFT, 26);
    text(d, titles[_settingsPage], LEFT, 70, 3);
    const char* notes[] = {
        _display.video.mode == MODE_GRAYSCALE ? "Fast grayscale: shades by partial drive doses." :
                                                "Fast dithered: four shades as a 4x4 Bayer pattern.",
        "Pulses for each change of shade (1-12).",
        "On: stop right after the drive scan (pair hold mode).",
        "Corrections for a pixel that returns to its shade.",
        "InkBoy presets in /inkboy/presets on the card"};
    text(d, notes[_settingsPage], LEFT, 104, 1);
    const std::vector<int> rows = settingsRows();
    for (unsigned i = 0; i < rows.size() && i < MAX_ROWS; ++i) {
        const int id = rows[i], y = ROW_Y + i * ROW_STEP;
        if (rowIsAction(id)) {
            d.drawRoundRect(LEFT, y, WIDTH, ROW_H, 8, GxEPD_BLACK);
            text(d, rowLabel(id), LEFT + 14, y + 17);
            text(d, ">", LEFT + WIDTH - 26, y + 17);
        } else {
            text(d, rowLabel(id), LEFT, y + 17);
            d.drawRoundRect(MINUS_X, y + 2, BUTTON_W, ROW_H - 4, 8, GxEPD_BLACK);
            d.drawRoundRect(PLUS_X, y + 2, BUTTON_W, ROW_H - 4, 8, GxEPD_BLACK);
            centered(d, "-", MINUS_X, BUTTON_W, y + 15, 3);
            centered(d, "+", PLUS_X, BUTTON_W, y + 15, 3);
            centered(d, rowValue(id), MINUS_X + BUTTON_W, PLUS_X - MINUS_X - BUTTON_W, y + 17);
        }
    }
    if (!_settingsMessage.isEmpty()) centered(d, _settingsMessage, LEFT, WIDTH, MESSAGE_Y, 1);
    else if (_settingsPage == PAGE_MAIN)
        centered(d, "Experimental drive: may cause image retention.", LEFT, WIDTH, MESSAGE_Y, 1);
    d.refresh(true);
}

void AppPaperboy::settingsTap(uint16_t x, uint16_t y) {
    if (y < 60) { back(); return; }
    if (y < ROW_Y) return;
    const int index = (y - ROW_Y) / ROW_STEP;
    if ((y - ROW_Y) % ROW_STEP >= ROW_H) return;
    const std::vector<int> rows = settingsRows();
    if (index >= int(rows.size()) || index >= MAX_ROWS) return;
    const int id = rows[index];
    if (rowIsAction(id)) { changeRow(id, 1); return; }
    // The - button steps down; + and the rest of the row step up.
    changeRow(id, x >= MINUS_X - 8 && x < MINUS_X + BUTTON_W + 8 ? -1 : 1);
}

// --- Transition test (InkBoy gray_motion) ------------------------------------------

void AppPaperboy::startTest() {
    _screen = SCREEN_TEST;
    _testPaused = false;
    paintTest();
    auto& d = DisplayMgr::getInstance().getDisplay();
    d.setGameSettings(_display.video);
    d.setGameMode(true);
    _testStarted = esp_timer_get_time();
    _nextImage = 0; _lastChange = _testStarted; _analogOn = true;
    _forceClean = true;
    _fpsCount = 0; _fpsAt = millis();
    _redraw = false;
}

void AppPaperboy::stopTest() {
    DisplayMgr::getInstance().getDisplay().setGameMode(false);
    _screen = SCREEN_SETTINGS;
    _redraw = true;
}

void AppPaperboy::paintTest() {
    auto& d = DisplayMgr::getInstance().getDisplay();
    d.setGameMode(false);
    d.setFullWindow(); d.fillScreen(GxEPD_WHITE);
    text(d, "< Display", LEFT, 26);
    using GameViewport::X; using GameViewport::Y;
    constexpr int BAND_PX = 36 * GameViewport::SCALE;
    for (int band = 0; band < 4; ++band) {
        const int top = Y + band * BAND_PX;
        text(d, String(SHADE[band]), X - 30, top + BAND_PX / 2 - 8);
        for (int lane = 0; lane < 3; ++lane)
            text(d, String(SHADE[motionPatch(band, lane)]), X + GameViewport::W + 12,
                 top + (lane * 12 + 2) * GameViewport::SCALE + 4, 1);
    }
    d.drawRect(X - 2, Y - 2, GameViewport::W + 4, GameViewport::H + 4, GxEPD_BLACK);
    const int y = Y + GameViewport::H + 24;
    text(d, "Transition test", LEFT, y, 3);
    text(d, _display.video.mode == MODE_GRAYSCALE ? "Fast grayscale" : "Fast dithered", LEFT, y + 40);
    text(d, "Each rectangle should match the shade named at", LEFT, y + 76, 1);
    text(d, "the right, and leave no trail behind.", LEFT, y + 92, 1);
    text(d, "Tap the picture to pause or resume.", LEFT, y + 116, 1);
    const int buttonY = y + 150;
    d.drawRoundRect(LEFT, buttonY, 210, 64, 8, GxEPD_BLACK);
    d.drawRoundRect(LEFT + 230, buttonY, 210, 64, 8, GxEPD_BLACK);
    centered(d, "Replay", LEFT, 210, buttonY + 24);
    centered(d, "Clean", LEFT + 230, 210, buttonY + 24);
    d.refresh(false);
}

void AppPaperboy::testTap(uint16_t x, uint16_t y) {
    if (y < 60) { back(); return; }
    using GameViewport::X; using GameViewport::Y;
    const int64_t now = esp_timer_get_time();
    if (x >= X && x < X + GameViewport::W && y >= Y && y < Y + GameViewport::H) {
        _testPaused = !_testPaused;
        if (_testPaused) _testFrozen = now - _testStarted;
        else _testStarted = now - _testFrozen;
        return;
    }
    const int buttonY = Y + GameViewport::H + 24 + 150;
    if (y >= buttonY && y < buttonY + 64) {
        if (x < LEFT + 220) { _testStarted = now; _testFrozen = 0; }
        else {
            // Clean: a full refresh re-establishes the panel reference.
            const int64_t started = _testStarted, frozen = _testFrozen;
            const bool paused = _testPaused;
            startTest();
            _testStarted = started; _testFrozen = frozen; _testPaused = paused;
        }
    }
}

void AppPaperboy::drawTest() {
    auto& d = DisplayMgr::getInstance().getDisplay();
    d.gameWaitSlot(_nextImage);
    const int64_t now = esp_timer_get_time();
    bool changed = false;
    if (now >= _nextImage) {
        motionRender(_workFrame, motionX(_testPaused ? _testFrozen : now - _testStarted));
        packVideo(_latest);
        changed = memcmp(_latest, _snapshot, packedBytes()) != 0 || _forceClean;
        _forceClean = false;
        std::swap(_snapshot, _latest);
        _nextImage = InkBoyDrive::nextImage(_nextImage, now);
    }
    d.setGameFrame(_snapshot);
    if (changed) _lastChange = now;
    if (changed || d.gamePending()) {
        d.refreshGame();
        _analogOn = true;
        countFps(changed);
    }
    if (!changed && !d.gamePending() && _analogOn && now - _lastChange >= InkBoyDrive::IDLE_POWER_OFF_US) {
        d.gamePowerOff();
        _analogOn = false;
    }
}

// USB serial tuning, one setting per line (the same ranges as the menu):
//   set mode gray|dither   set show_fps 0|1   set black N   set white N
//   set dose KL N (from, to: K D L W)   set hold KL 0|1   set retarget 50..150
//   set pair 0|1   set policy auto|one   set history 0..3
//   set return LKL N   set reverse LKL N (0 = base)   show
void AppPaperboy::serialSetting(const char* line) {
    Settings& v = _display.video;
    char a[12] = {}, b[12] = {};
    int n = 0;
    auto shade = [](char c) { for (int i = 0; i < 4; ++i) if (SHADE[i] == c) return i; return -1; };
    auto route = [](const char* s) {
        for (int r = 0; r < ROUTES; ++r) {
            const char* l = ROUTE_LABELS[r];
            if (s[0] == l[0] && s[1] == l[2] && s[2] == l[4] && !s[3]) return r;
        }
        return -1;
    };
    bool ok = true, modeSetting = true;
    if (strncmp(line, "set ", 4) == 0 && (_playing || _paused)) {
        Serial.println("Inkade: close the game before changing display settings");
        return;
    }
    if (!strcmp(line, "show")) {
    } else if (sscanf(line, "set mode %11s", a) == 1) {
        v.mode = !strcmp(a, "dither") ? MODE_DITHERED : MODE_GRAYSCALE; modeSetting = false;
    } else if (sscanf(line, "set show_fps %d", &n) == 1) { v.showFps = n != 0; modeSetting = false;
    } else if (sscanf(line, "set black %d", &n) == 1) { v.blackPulses = n;
    } else if (sscanf(line, "set white %d", &n) == 1) { v.whitePulses = n;
    } else if (sscanf(line, "set retarget %d", &n) == 1) { v.gray.retargetGain = n >= 50 ? (n - 50) / 25 : 0;
    } else if (sscanf(line, "set pair %d", &n) == 1) { v.immediate = n != 0;
    } else if (sscanf(line, "set policy %11s", a) == 1) { v.gray.scanPolicy = !strcmp(a, "one");
    } else if (sscanf(line, "set history %d", &n) == 1) { v.adaptive.historyMode = n;
    } else if (sscanf(line, "set dose %2s %d", a, &n) == 2 && shade(a[0]) >= 0 && shade(a[1]) >= 0) {
        v.gray.dose[shade(a[0])][shade(a[1])] = n;
    } else if (sscanf(line, "set hold %2s %d", a, &n) == 2 && shade(a[0]) >= 0 && shade(a[1]) >= 0) {
        const unsigned bit = 1u << (shade(a[0]) * 4 + shade(a[1]));
        v.holds = n ? (v.holds | bit) : (v.holds & ~bit);
    } else if (sscanf(line, "set return %11s %d", b, &n) == 2 && route(b) >= 0) {
        v.adaptive.returnDelta[route(b)] = n;
    } else if (sscanf(line, "set reverse %11s %d", b, &n) == 2 && route(b) >= 0) {
        v.adaptive.reversePercent[route(b)] = n;
    } else {
        ok = false;
    }
    if (!ok) { Serial.printf("Inkade: unknown command '%s'\n", line); return; }
    if (strcmp(line, "show")) {
        settingsChanged(modeSetting);
        if (_screen == SCREEN_TEST) startTest();  // the new settings take effect
    }
    Serial.printf("Inkade settings: %s, black %u white %u, retarget %u%%, pair %u, policy %s, holds 0x%04X, "
                  "history %u, fps %s\n", v.mode == MODE_GRAYSCALE ? "gray" : "dither", v.blackPulses,
                  v.whitePulses, retargetPercent(v.gray), v.immediate, v.gray.scanPolicy ? "one" : "auto",
                  v.holds, v.adaptive.historyMode, v.showFps ? "shown" : "hidden");
    for (int from = 0; from < 4; ++from)
        Serial.printf("  from %c: K %u  D %u  L %u  W %u\n", SHADE[from], v.gray.dose[from][0],
                      v.gray.dose[from][1], v.gray.dose[from][2], v.gray.dose[from][3]);
    for (int r = 0; r < ROUTES; ++r)
        Serial.printf("  %s return %+d reverse %u%%\n", ROUTE_LABELS[r], v.adaptive.returnDelta[r],
                      v.adaptive.reversePercent[r]);
}
#endif
