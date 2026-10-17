// Page books in the eReader: CBZ manga and PDFs converted on upload, shown
// one page image at a time (PageBook.h), with pinch to zoom (PageView.h).
//
// Touch:   pinch zooms around the fingers, one finger drags a zoomed page,
//          a swipe turns the page, a tap on the left or right third turns
//          the page (or, zoomed in, moves one screen along in reading
//          order), a tap in the middle zooms in or back out.
// Buttons: next and previous work like the right and left taps.
// Manga order (right to left) mirrors all of it.
#include "AppReader.h"

#if BOOK32_HAS_PAGE_BOOKS

#include "BatteryMgr.h"
#include "DisplayMgr.h"
#include "EpubImage.h"
#include "FontMgr.h"
#include "InputMgr.h"
#include <Fonts/FreeSans9pt7b.h>

namespace {
constexpr unsigned long PREVIEW_INTERVAL_MS = 300;  // Live redraws while fingers move
constexpr unsigned long RELEASE_MS = 80;            // All fingers up this long ends a gesture
constexpr unsigned long TAP_AFTER_GESTURE_MS = 400; // A tap this soon after a pinch or drag is its own end
constexpr int DRAG_THRESHOLD = 24;                  // InputMgr's tap limit
constexpr int SWIPE_MIN = 70;
constexpr int PAGE_SCALE = 2;                       // Pages are decoded at up to twice the screen

struct Box { int x = 0, y = 0, w = 0, h = 0; };

// The page number and percentage, in a small white box at the bottom.
Box footerBox(Book32Display& display, const char* text) {
    Box box;
    if (!text[0]) return box;
    display.setFont(&FreeSans9pt7b);
    int16_t bx, by;
    uint16_t bw, bh;
    display.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    box.w = bw + 20;
    box.h = bh + 12;
    box.x = (display.width() - box.w) / 2;
    box.y = display.height() - box.h - 4;
    return box;
}

void drawFooter(Book32Display& display, const char* text) {
    Box box = footerBox(display, text);
    if (!box.w) return;
    int16_t bx, by;
    uint16_t bw, bh;
    display.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    display.fillRoundRect(box.x, box.y, box.w, box.h, 6, GxEPD_WHITE);
    display.drawRoundRect(box.x, box.y, box.w, box.h, 6, GxEPD_BLACK);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(box.x + 10 - bx, box.y + 6 - by);
    display.print(text);
    display.setFont(nullptr);
}
}  // namespace

bool AppReader::openPageBook(const String& path, bool restoreProgress) {
    _pageBook = new (std::nothrow) PageBook();
    if (!_pageBook || !_pageBook->open("/ebooks" + path)) {
        delete _pageBook;
        _pageBook = nullptr;
        _currentBookPath = "";
        _currentBookSize = 0;
        _currentBookFingerprint = 0;
        _libraryFirstDraw = true;
        _needsRedraw = true;
        return false;
    }
    _pageIndex = restoreProgress ? loadPageProgress(path) : 0;
    _pageIndex = constrain(_pageIndex, 0, _pageBook->pageCount() - 1);
    drawLoadingScreen("Opening book", "Loading page...", 70, false);
    loadPageImage(false, true);
    _gestureActive = false;
    _state = VIEW_READING;
    _readingFirstDraw = true;
    saveReadingProgress(true);
    _needsRedraw = true;
    return true;
}

void AppReader::closePageBook() {
    freePage();
    freePrepared(_nextPage);
    delete _pageBook;
    _pageBook = nullptr;
    _gestureActive = false;
}

void AppReader::freePage() {
    free(_pageData);
    free(_pageFit);
    free(_pageGray);
    _pageData = _pageFit = _pageGray = nullptr;
    _pageDataSize = 0;
    _pageFitW = _pageFitH = _pageGrayW = _pageGrayH = 0;
}

void AppReader::freePrepared(PreparedPage& page) {
    free(page.data);
    free(page.fit);
    page = PreparedPage();
}

void AppReader::preparePage(int index, PreparedPage& page) {
    const unsigned long startedAt = millis();
    BatteryMgr::getInstance().resetIdleTimer();  // Full CPU speed for decoding
    freePrepared(page);
    page.index = index;
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    const int screenW = display.width(), screenH = display.height();
    page.data = _pageBook->readPage(index, page.size);
    const unsigned long readMs = millis() - startedAt;
    // width x height is the full-size picture, which PageView works in.
    if (page.data && EpubImageDecoder::fittedSize(page.data, page.size, PAGE_SCALE * screenW,
                                                  PAGE_SCALE * screenH, page.width, page.height)) {
        page.fit = EpubImageDecoder::decodeGray(page.data, page.size, screenW, screenH, page.fitW, page.fitH);
    }
    if (!page.fit) page.width = page.height = 0;
    Serial.printf("AppReader: page %d/%d, %dx%d shown at %dx%d, %u bytes, read in %lums, decoded in %lums\n",
                  index + 1, _pageBook->pageCount(), page.width, page.height, page.fitW, page.fitH,
                  static_cast<unsigned>(page.size), readMs, millis() - startedAt - readMs);
}

void AppReader::prefetchNextPage() {
    const int next = _pageIndex + (_lastTurnForward ? 1 : -1);
    if (next < 0 || next >= _pageBook->pageCount() || _nextPage.index == next) return;
    preparePage(next, _nextPage);
}

bool AppReader::loadPageImage(bool keepZoom, bool forward) {
    freePage();
    PreparedPage page;
    if (_nextPage.index == _pageIndex) std::swap(page, _nextPage);
    else preparePage(_pageIndex, page);
    _pageData = page.data;
    _pageDataSize = page.size;
    _pageFit = page.fit;
    _pageFitW = page.fitW;
    _pageFitH = page.fitH;
    const int width = page.width, height = page.height;
    page = PreparedPage();  // Owned by the current page now
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    const int screenW = display.width(), screenH = display.height();
    const float zoom = _pageView.zoom;
    _pageView.setPage(max(width, 1), max(height, 1), screenW, screenH);
    if (keepZoom && _pageFit) {
        _pageView.zoom = zoom;
        _pageView.startOfPage(forward, _rightToLeft);
    }
    return _pageFit != nullptr;
}

bool AppReader::ensureFullPage() {
    if (_pageGray) return true;
    if (!_pageData) return false;
    const unsigned long startedAt = millis();
    BatteryMgr::getInstance().resetIdleTimer();
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    _pageGray = EpubImageDecoder::decodeGray(_pageData, _pageDataSize, PAGE_SCALE * display.width(),
                                             PAGE_SCALE * display.height(), _pageGrayW, _pageGrayH);
    Serial.printf("AppReader: full page %dx%d decoded in %lums\n", _pageGrayW, _pageGrayH, millis() - startedAt);
    return _pageGray != nullptr;
}

bool AppReader::turnPageBook(bool forward, bool keepZoom) {
    const int next = _pageIndex + (forward ? 1 : -1);
    if (next < 0 || next >= _pageBook->pageCount()) return false;
    _pageIndex = next;
    _lastTurnForward = forward;
    loadPageImage(keepZoom, forward);
    queueProgressSave();
    _needsRedraw = true;
    return true;
}

void AppReader::stepPageBook(bool forward) {
    BatteryMgr::getInstance().resetIdleTimer();
    if (_pageView.zoomed() && _pageView.step(forward, _rightToLeft)) {
        _needsRedraw = true;
        return;
    }
    turnPageBook(forward, _pageView.zoomed());
}

void AppReader::pageBookTouch(uint16_t x, uint16_t y) {
    // InputMgr reports a tap when the first finger barely moved, which is
    // also how a pinch around a still finger ends.
    if (_gestureConsumed && (_gestureActive || millis() - _gestureEndedAt < TAP_AFTER_GESTURE_MS)) return;
    const int width = DisplayMgr::getInstance().getDisplay().width();
    const bool left = x < width / 3, right = x >= (width * 2) / 3;
    if (!left && !right) {
        if (_pageView.zoomed()) _pageView.reset();
        else _pageView.zoomAt(PageView::TAP_ZOOM, x, y);
        _needsRedraw = true;
        return;
    }
    stepPageBook(right != _rightToLeft);
}

void AppReader::pollPageGestures() {
    InputMgr::TouchPoint points[5];
    const uint8_t count = InputMgr::getInstance().heldTouches(points);
    const unsigned long now = millis();
    if (count == 0) {
        if (!_gestureActive) return;
        if (!_gestureZeroSince) _gestureZeroSince = now;
        if (now - _gestureZeroSince < RELEASE_MS) return;  // A missed report, not a release
        _gestureActive = false;
        _gestureEndedAt = now;
        const int dx = _dragLastX - _dragStartX, dy = _dragLastY - _dragStartY;
        if (_gestureFingers == 1 && _gestureMoved && !_pageView.zoomed() &&
            abs(dx) >= SWIPE_MIN && abs(dx) * 2 > abs(dy) * 3) {
            turnPageBook((dx < 0) != _rightToLeft, false);
            return;
        }
#if defined(BOARD_LILYGO_T5S3_PRO)
        const bool grayFinish = _grayPages && _gesturePreviewed;  // Previews are dithered
#else
        const bool grayFinish = false;
#endif
        if (_gestureViewChanged || grayFinish) _needsRedraw = true;
        return;
    }
    _gestureZeroSince = 0;
    BatteryMgr::getInstance().resetIdleTimer();
    const int width = DisplayMgr::getInstance().getDisplay().width();
    const int height = DisplayMgr::getInstance().getDisplay().height();

    if (!_gestureActive) {
        _gestureActive = true;
        _gestureMoved = _gestureConsumed = _gestureViewChanged = _gesturePreviewed = false;
        _pinchActive = false;
        _gestureFingers = count;
        _dragStartX = _dragLastX = points[0].x;
        _dragStartY = _dragLastY = points[0].y;
        _lastPreviewAt = now;
    }
    _gestureFingers = max(_gestureFingers, count);

    if (count >= 2) {
        const float mx = (points[0].x + points[1].x) / 2.0f, my = (points[0].y + points[1].y) / 2.0f;
        const float distance = hypotf(float(points[0].x) - points[1].x, float(points[0].y) - points[1].y);
        if (!_pinchActive) {
            _pinchActive = true;
            _gestureConsumed = true;
            _pinchStartDistance = max(distance, 20.0f);
            _pinchStartZoom = _pageView.zoom;
            _pageView.toPage(mx, my, _pinchPageX, _pinchPageY);
            ensureFullPage();  // Zooming in shows the full-size picture
        } else {
            const float oldZoom = _pageView.zoom, oldX = _pageView.cx, oldY = _pageView.cy;
            // The page point first between the fingers stays between them.
            _pageView.zoom = fminf(fmaxf(_pinchStartZoom * distance / _pinchStartDistance, 1.0f), PageView::MAX_ZOOM);
            _pageView.cx = _pinchPageX - (mx - width / 2.0f) / _pageView.scale();
            _pageView.cy = _pinchPageY - (my - height / 2.0f) / _pageView.scale();
            _pageView.clamp();
            if (fabsf(_pageView.zoom - oldZoom) > 0.02f || fabsf(_pageView.cx - oldX) > 1 || fabsf(_pageView.cy - oldY) > 1)
                _gestureViewChanged = _viewChangedSincePreview = true;
        }
    } else {
        const int x = points[0].x, y = points[0].y;
        if (_pinchActive) {  // Back to one finger: drag on from here
            _pinchActive = false;
            _dragLastX = x;
            _dragLastY = y;
        }
        if (!_gestureMoved && (abs(x - _dragStartX) > DRAG_THRESHOLD || abs(y - _dragStartY) > DRAG_THRESHOLD)) {
            _gestureMoved = true;
            _gestureConsumed = true;
        }
        if (_gestureMoved && _pageView.zoomed() && (x != _dragLastX || y != _dragLastY)) {
            _pageView.panBy(x - _dragLastX, y - _dragLastY);
            _gestureViewChanged = _viewChangedSincePreview = true;
        }
        _dragLastX = x;
        _dragLastY = y;
    }

    if (_viewChangedSincePreview && now - _lastPreviewAt >= PREVIEW_INTERVAL_MS) {
        drawPageBook(true);
        _lastPreviewAt = millis();
    }
}

void AppReader::drawPageBook(bool preview) {
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    const int screenW = display.width(), screenH = display.height();
    const unsigned long startedAt = millis();
    BatteryMgr::getInstance().resetIdleTimer();
    _viewChangedSincePreview = false;
    if (preview) _gesturePreviewed = true;

    bool full = false;
    if (!preview && (_readingFirstDraw || _pageTurnsSinceRefresh >= _refreshEveryNPages)) {
        full = true;
        _readingFirstDraw = false;
        _pageTurnsSinceRefresh = 0;
    } else if (!preview) {
        _pageTurnsSinceRefresh++;
    }

    char footer[32] = "";
    if (!_pageView.zoomed() && _pageBook) {
        const int pages = _pageBook->pageCount();
        const int percent = pages > 1 ? (_pageIndex * 100 + (pages - 1) / 2) / (pages - 1) : 100;
        if (_showPageNumber && _showReadingPercentage)
            snprintf(footer, sizeof(footer), "%d / %d    %d%%", _pageIndex + 1, pages, percent);
        else if (_showPageNumber) snprintf(footer, sizeof(footer), "%d / %d", _pageIndex + 1, pages);
        else if (_showReadingPercentage) snprintf(footer, sizeof(footer), "%d%%", percent);
    }

    if (!_pageFit) {
        FontMgr& fonts = FontMgr::getInstance();
        display.setFullWindow();
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE);
            fonts.drawTextCentered(display, "This page can't be shown", screenH / 2 - 24, FONT_SIZE_BODY);
            fonts.drawTextCentered(display, "Upload the book again on the web page", screenH / 2 + 12, FONT_SIZE_SMALL);
            fonts.drawTextCentered(display, "so InkDeck can convert it.", screenH / 2 + 36, FONT_SIZE_SMALL);
            drawFooter(display, footer);
        } while (display.nextPage());
        _readingFirstDraw = true;
        return;
    }

    // The visible part of the page, scaled to the screen, on white.
    const size_t screenBytes = static_cast<size_t>(screenW) * screenH;
    uint8_t* screen = static_cast<uint8_t*>(ps_malloc(screenBytes));
    if (!screen) return;
    memset(screen, 255, screenBytes);
    // PageView works in full-size pixels; unzoomed, the screen-size picture
    // is used instead (usually it is copied as it is).
    int x, y, w, h, dx, dy, dw, dh;
    _pageView.visible(x, y, w, h, dx, dy, dw, dh);
    const bool useFull = _pageView.zoomed() && ensureFullPage();
    const uint8_t* source = useFull ? _pageGray : _pageFit;
    const int sourceW = useFull ? _pageGrayW : _pageFitW, sourceH = useFull ? _pageGrayH : _pageFitH;
    const float fx = static_cast<float>(sourceW) / _pageView.pageW, fy = static_cast<float>(sourceH) / _pageView.pageH;
    const int sx = constrain(static_cast<int>(x * fx), 0, sourceW - 1), sy = constrain(static_cast<int>(y * fy), 0, sourceH - 1);
    const int sw = constrain(static_cast<int>(lroundf(w * fx)), 1, sourceW - sx);
    const int sh = constrain(static_cast<int>(lroundf(h * fy)), 1, sourceH - sy);
    if (dw > 0 && dh > 0 && sw == dw && sh == dh) {
        for (int row = 0; row < dh; row++)
            memcpy(screen + static_cast<size_t>(dy + row) * screenW + dx,
                   source + static_cast<size_t>(sy + row) * sourceW + sx, dw);
    } else if (dw > 0 && dh > 0) {
        uint8_t* region = static_cast<uint8_t*>(ps_malloc(static_cast<size_t>(dw) * dh));
        if (region && EpubImageDecoder::resampleRegion(source, sourceW, sx, sy, sw, sh, region, dw, dh)) {
            for (int row = 0; row < dh; row++)
                memcpy(screen + static_cast<size_t>(dy + row) * screenW + dx, region + static_cast<size_t>(row) * dw, dw);
        }
        free(region);
    }

#if defined(BOARD_LILYGO_T5S3_PRO)
    // Finished views in 16 grays (one full refresh); previews stay dithered
    // and fast.
    if (_grayPages && !preview) {
        Box box = footerBox(display, footer);
        display.setFullWindow();
        display.fillScreen(GxEPD_WHITE);
        if (box.w) {  // The footer goes into the gray image as black and white
            drawFooter(display, footer);
            for (int row = box.y; row < box.y + box.h; row++)
                for (int col = box.x; col < box.x + box.w; col++)
                    screen[static_cast<size_t>(row) * screenW + col] = display.getPixel(col, row) ? 255 : 0;
            display.fillScreen(GxEPD_WHITE);
        }
        uint8_t* copy = static_cast<uint8_t*>(ps_malloc(screenBytes));  // drawGrayCover overwrites its input
        if (copy) {
            memcpy(copy, screen, screenBytes);
            const bool shown = display.drawGrayCover(copy, 0, 0, screenW, screenH);
            free(copy);
            if (shown) {
                free(screen);
                _pageTurnsSinceRefresh = 0;  // Every gray view is a full refresh
                _pageDrawnAt = millis();
                Serial.printf("AppReader: page view (gray) in %lums\n", millis() - startedAt);
                return;
            }
        }
    }
#endif

    EpubBitmap bitmap;
    const bool dithered = EpubImageDecoder::dither(screen, screenW, screenH, bitmap);
    free(screen);
    if (full) display.setFullWindow();
    else display.setPartialWindow(0, 0, screenW, screenH);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        if (dithered) bitmap.draw(display, 0, 0);
        drawFooter(display, footer);
    } while (display.nextPage());
    Serial.printf("AppReader: page view (%s%s, zoom %.2f) in %lums\n", preview ? "preview" : "final",
                  full ? ", full refresh" : "", _pageView.zoom, millis() - startedAt);
    _pageDrawnAt = millis();
}

#endif
