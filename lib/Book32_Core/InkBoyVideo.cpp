#include "InkBoyVideo.h"

#include <Arduino.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_timer.h>

using namespace InkBoyDrive;

namespace {
void* internal(size_t bytes) {
    void* p = heap_caps_calloc(1, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void sleepUntil(int64_t deadline) {
    const int64_t remaining = deadline - esp_timer_get_time();
    if (remaining > 0) vTaskDelay(pdMS_TO_TICKS((remaining + 999) / 1000));
}
}

void InkBoyVideo::setSettings(const Settings& settings) {
    _nextSettings = settings;
    sanitize(_nextSettings);
}

bool InkBoyVideo::allocate() {
    if (!_newPlane) _newPlane = static_cast<uint8_t*>(internal(PLANE_BYTES));
    if (!_oldPlane) _oldPlane = static_cast<uint8_t*>(internal(PLANE_BYTES));
    if (!_maskBlack) _maskBlack = static_cast<uint8_t*>(internal(PLANE_BYTES));
    if (!_maskWhite) _maskWhite = static_cast<uint8_t*>(internal(PLANE_BYTES));
    // The two modes' workspaces compete for internal RAM: free the other one.
    if (gray()) {
        free(_counters); _counters = nullptr;
        if (!_grayPlan) _grayPlan = static_cast<GrayPlan*>(internal(sizeof(GrayPlan)));
        if (_grayPlan && !_grayPixels) _grayPixels = static_cast<uint16_t*>(internal(PIXELS * sizeof(uint16_t)));
        if (!_followBlack) _followBlack = static_cast<uint8_t*>(internal(MASK_BYTES));
        if (!_followWhite) _followWhite = static_cast<uint8_t*>(internal(MASK_BYTES));
        const bool history = _settings.adaptive.historyMode != 0;
        if (history && !_grayHistory)
            _grayHistory = static_cast<GrayHistory*>(
                heap_caps_calloc(PIXELS, sizeof(GrayHistory), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!history) { free(_grayHistory); _grayHistory = nullptr; }
        return _newPlane && _oldPlane && _maskBlack && _maskWhite && _grayPlan && _grayPixels && _followBlack &&
               _followWhite && (!history || _grayHistory);
    }
    free(_grayPlan); _grayPlan = nullptr;
    free(_grayPixels); _grayPixels = nullptr;
    free(_grayHistory); _grayHistory = nullptr;
    free(_followBlack); _followBlack = nullptr;
    free(_followWhite); _followWhite = nullptr;
    if (!_counters) _counters = static_cast<DriveCounters*>(internal(sizeof(DriveCounters)));
    return _newPlane && _oldPlane && _maskBlack && _maskWhite && _counters;
}

bool InkBoyVideo::start(InkBoyPanel* panel) {
    _settings = _nextSettings;
    if (!panel || !allocate()) {
        Serial.println("InkBoyVideo: not enough memory for game video");
        return false;
    }
    if (gray()) {
        memset(_grayPlan, 0, sizeof(GrayPlan));
        _grayPlan->pixel = _grayPixels;
        _grayPlan->history = _grayHistory;
        _grayPlan->config = _settings.gray;
        _grayPlan->adaptive = _settings.adaptive;
        _grayPlan->seed(3);  // InkBoy W: the window starts white
    } else {
        memset(_newPlane, 0xFF, PLANE_BYTES);
        _counters->seed(_newPlane);
    }
    _holdEndPending = false;
    _lastStart = 0;
    _scanCost = gray() ? 14000 : 40000;
    memset(_costs, 0, sizeof(_costs));
    _costNext = 0;
    if (!panel->videoStart(gray())) return false;
    _panel = panel;
    Serial.printf("InkBoyVideo: %s drive%s\n", gray() ? "grayscale" : "dithered",
                  gray() && _settings.immediate ? " (pair hold)" : "");
    return true;
}

void InkBoyVideo::stop() {
    if (_panel) _panel->videoPowerOff();
    _panel = nullptr;
    _image = nullptr;
}

unsigned InkBoyVideo::pending() const {
    if (!_panel) return 0;
    if (gray()) return _grayPlan ? (_grayPlan->pending ? _grayPlan->pending : _holdEndPending ? 1 : 0) : 0;
    if (!_counters) return 0;
    if (_holdEndPending && !_counters->pendingPixels) return 1;  // HOLD ending still to deliver
    return _counters->pendingPixels;
}

void InkBoyVideo::waitSlot(int64_t imageDeadline) {
    if (!_panel) return;
    for (;;) {
        const int64_t now = esp_timer_get_time();
        const int64_t ready = _lastStart ? _lastStart + drivePeriodUs(gray()) : now;
        int64_t wake = imageDeadline > ready ? imageDeadline : ready;
        if (gray() && !_settings.gray.scanPolicy && !_settings.immediate)
            wake = wakeTime(now, ready, imageDeadline, pending() != 0, _scanCost);
        if (now >= wake) return;
        vTaskDelay(pdMS_TO_TICKS((wake - now + 999) / 1000));
    }
}

bool InkBoyVideo::scan() { return _panel->videoScan(_oldPlane, _newPlane); }

bool InkBoyVideo::refresh() {
    if (!_panel || !_image) return _panel != nullptr;
    return gray() ? refreshGray() : refreshDithered();
}

// fast_display_update, dithered.
bool InkBoyVideo::refreshDithered() {
    if (!_holdEndPending && !_counters->pendingPixels && !memcmp(_counters->target, _image, PLANE_BYTES)) return true;
    if (_lastStart) sleepUntil(_lastStart + drivePeriodUs(false));  // 30 ms drive period
    const bool wasPrepared = _lastStart != 0;
    const int64_t start = esp_timer_get_time();
    const unsigned driven = _counters->prepare(_image, _settings.blackPulses, _settings.whitePulses,
                                               _maskBlack, _maskWhite);
    panelMasksToPlanes(_maskBlack, _maskWhite, _newPlane, _oldPlane);
    if (!scan()) return false;
    _counters->complete(_maskBlack, _maskWhite);
    // Keep a successor pending until an all-HOLD mask is delivered.
    _holdEndPending = driven != 0;
    const int64_t cost = esp_timer_get_time() - start + 2000;
    if (wasPrepared && cost > _scanCost) _scanCost = cost;
    _lastStart = start;
    return true;
}

// gray_update: grayscale, with pair hold mode.
bool InkBoyVideo::refreshGray() {
    const int64_t period = (_settings.gray.scanPolicy || _settings.immediate) ? 30000 : drivePeriodUs(true);
    if (_lastStart) sleepUntil(_lastStart + period);
    const int64_t start = esp_timer_get_time();
    const bool immediate = _settings.immediate;
    _grayPlan->target(_image);
    const unsigned driven = _grayPlan->masks(_maskBlack, _maskWhite, immediate ? _followBlack : nullptr,
                                             immediate ? _followWhite : nullptr, _settings.holds);
    if (!_grayPlan->pending && !_holdEndPending) return true;
    masksToPlanes(_maskBlack, _maskWhite, _newPlane, _oldPlane);
    if (!scan()) return false;
    unsigned followDriven = 0;
    bool error = false;
    if (immediate && driven) {
        // The follow-up scan right behind the drive scan.
        for (int i = 0; i < MASK_BYTES; ++i) followDriven += __builtin_popcount(unsigned(_followBlack[i] | _followWhite[i]));
        masksToPlanes(_followBlack, _followWhite, _newPlane, _oldPlane);
        error = !scan();
    }
    if (!error && followDriven) _grayPlan->complete(_maskBlack, _maskWhite, _followBlack, _followWhite);
    else _grayPlan->complete(_maskBlack, _maskWhite);
    // A follow-up that still drove pixels needs a later HOLD ending; a
    // zero-mask follow-up has already delivered it.
    _holdEndPending = driven != 0 && (!immediate || error || followDriven);
    if (error) return false;
    const int64_t end = esp_timer_get_time();
    if (_lastStart) {
        _costs[_costNext++ % 8] = end - start + 1000;
        _scanCost = 14000;
        for (int64_t c : _costs) if (c > _scanCost) _scanCost = c;
    }
    _lastStart = start;
    return true;
}
