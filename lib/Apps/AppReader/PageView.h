#pragma once

#include <math.h>

// Zoom and pan of one page image on the screen, for page books (manga and
// converted PDFs). Zoom 1 fits the whole page; (cx, cy) is the page point at
// the middle of the screen. Plain arithmetic, so it is easy to reason about:
// every change ends in clamp(), which keeps the view on the page.
struct PageView {
    static constexpr float MAX_ZOOM = 4.0f;
    static constexpr float TAP_ZOOM = 2.0f;   // A tap in the middle zooms to this
    static constexpr float OVERLAP = 0.15f;   // Kept on screen when stepping across a page

    int pageW = 0, pageH = 0, screenW = 0, screenH = 0;
    float zoom = 1.0f;
    float cx = 0.0f, cy = 0.0f;

    void setPage(int width, int height, int screenWidth, int screenHeight) {
        pageW = width; pageH = height; screenW = screenWidth; screenH = screenHeight;
        reset();
    }
    void reset() { zoom = 1.0f; cx = pageW / 2.0f; cy = pageH / 2.0f; }
    bool zoomed() const { return zoom > 1.01f; }

    float fitScale() const {
        if (pageW <= 0 || pageH <= 0) return 1.0f;
        return fminf(static_cast<float>(screenW) / pageW, static_cast<float>(screenH) / pageH);
    }
    float scale() const { return fitScale() * zoom; }
    float viewW() const { return screenW / scale(); }   // Visible page width, in page pixels
    float viewH() const { return screenH / scale(); }

    void clamp() {
        zoom = fminf(fmaxf(zoom, 1.0f), MAX_ZOOM);
        const float vw = viewW(), vh = viewH();
        cx = vw >= pageW ? pageW / 2.0f : fminf(fmaxf(cx, vw / 2), pageW - vw / 2);
        cy = vh >= pageH ? pageH / 2.0f : fminf(fmaxf(cy, vh / 2), pageH - vh / 2);
    }

    void toPage(float sx, float sy, float& px, float& py) const {
        px = cx + (sx - screenW / 2.0f) / scale();
        py = cy + (sy - screenH / 2.0f) / scale();
    }

    // Zoom, keeping the page point under screen point (sx, sy) where it is.
    void zoomAt(float newZoom, float sx, float sy) {
        float px, py;
        toPage(sx, sy, px, py);
        zoom = fminf(fmaxf(newZoom, 1.0f), MAX_ZOOM);
        cx = px - (sx - screenW / 2.0f) / scale();
        cy = py - (sy - screenH / 2.0f) / scale();
        clamp();
    }

    // Move the page with a finger: (dx, dy) in screen pixels.
    void panBy(float dx, float dy) {
        cx -= dx / scale();
        cy -= dy / scale();
        clamp();
    }

    // The visible part of the page, (x, y, w, h) in page pixels, and where it
    // goes on the screen, (dx, dy, dw, dh). Outside it the screen is white.
    void visible(int& x, int& y, int& w, int& h, int& dx, int& dy, int& dw, int& dh) const {
        const float s = scale();
        const float left = cx - viewW() / 2, top = cy - viewH() / 2;
        const float x0 = fmaxf(0, left), y0 = fmaxf(0, top);
        const float x1 = fminf(pageW, left + viewW()), y1 = fminf(pageH, top + viewH());
        x = static_cast<int>(floorf(x0));
        y = static_cast<int>(floorf(y0));
        w = static_cast<int>(ceilf(x1)) - x;
        h = static_cast<int>(ceilf(y1)) - y;
        if (x + w > pageW) w = pageW - x;
        if (y + h > pageH) h = pageH - y;
        dx = static_cast<int>(lroundf((x - left) * s));
        dy = static_cast<int>(lroundf((y - top) * s));
        dw = static_cast<int>(lroundf(w * s));
        dh = static_cast<int>(lroundf(h * s));
        if (dx < 0) dx = 0;
        if (dy < 0) dy = 0;
        if (dx + dw > screenW) dw = screenW - dx;
        if (dy + dh > screenH) dh = screenH - dy;
    }

    // Reading order while zoomed in: across the page one screen at a time
    // (right to left for manga), then down to the next row. Returns false at
    // the end of the page (the start, going back) so the caller turns the page.
    bool step(bool forward, bool rightToLeft) {
        clamp();
        const float vw = viewW(), vh = viewH();
        const float minX = fminf(vw / 2, pageW / 2.0f), maxX = fmaxf(pageW - vw / 2, pageW / 2.0f);
        const float minY = fminf(vh / 2, pageH / 2.0f), maxY = fmaxf(pageH - vh / 2, pageH / 2.0f);
        const bool right = forward != rightToLeft;
        if (right ? cx < maxX - 0.5f : cx > minX + 0.5f) {
            cx += (right ? 1 : -1) * vw * (1 - OVERLAP);
            clamp();
            return true;
        }
        if (forward ? cy < maxY - 0.5f : cy > minY + 0.5f) {
            cy += (forward ? 1 : -1) * vh * (1 - OVERLAP);
            cx = right ? minX : maxX;  // The start of the row in reading order
            clamp();
            return true;
        }
        return false;
    }

    // Where reading starts on a new page at the current zoom: the first
    // corner in reading order, or the last one when going back.
    void startOfPage(bool forward, bool rightToLeft) {
        clamp();
        const float vw = viewW(), vh = viewH();
        const bool right = forward != rightToLeft;
        cx = right ? vw / 2 : pageW - vw / 2;
        cy = forward ? vh / 2 : pageH - vh / 2;
        clamp();
    }
};
