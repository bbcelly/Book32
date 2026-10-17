#pragma once

#include <Arduino.h>
#include "DisplayMgr.h"

struct EpubImageInfo {
    int width = 0;
    int height = 0;
};

class EpubBitmap {
public:
    EpubBitmap() = default;
    ~EpubBitmap();
    EpubBitmap(const EpubBitmap&) = delete;
    EpubBitmap& operator=(const EpubBitmap&) = delete;
    EpubBitmap(EpubBitmap&& other) noexcept;
    EpubBitmap& operator=(EpubBitmap&& other) noexcept;

    bool valid() const { return _pixels != nullptr && width > 0 && height > 0; }
    bool allocate(int bitmapWidth, int bitmapHeight);
    uint8_t* pixels() { return _pixels; }
    int stride() const { return _stride; }
    void clear();
    void draw(Book32Display& display, int x, int y) const;
    // Cover decodes also keep the 8-bit gray image (0 black .. 255 white,
    // width x height) the 1-bit picture was dithered from, for displays that
    // can show gray directly.
    uint8_t* gray() { return _gray; }
    void keepGray(uint8_t* gray);

    int width = 0;
    int height = 0;

private:
    uint8_t* _pixels = nullptr;
    uint8_t* _gray = nullptr;
    int _stride = 0;
};

// Thumbnail: library covers and inline book images (crisp Atkinson dither).
// Cover: full-screen sleep covers (serpentine Floyd-Steinberg, which keeps
// smooth tones on large images). Cover mode refuses progressive JPEGs, whose
// decoder path can overrun its buffers, so callers fall back safely.
enum class EpubImageQuality { Thumbnail, Cover };

class EpubImageDecoder {
public:
    static bool dimensions(const uint8_t* data, size_t size, EpubImageInfo& info);
    static bool decode(const uint8_t* data, size_t size, int maxWidth, int maxHeight, EpubBitmap& bitmap,
                       EpubImageQuality quality = EpubImageQuality::Thumbnail);

    // Page images (manga and converted PDFs): the whole image as 8-bit gray
    // (0 black .. 255 white) fitted inside maxWidth x maxHeight, levels
    // stretched. Returns a PSRAM buffer the caller frees, or nullptr.
    // Progressive JPEGs are refused (see Cover above).
    static uint8_t* decodeGray(const uint8_t* data, size_t size, int maxWidth, int maxHeight,
                               int& width, int& height);
    // The size decodeGray would give, from the image header alone.
    static bool fittedSize(const uint8_t* data, size_t size, int maxWidth, int maxHeight, int& width, int& height);
    // Area-averages (shrinking) or interpolates (enlarging) the rectangle
    // (x, y, w, h) of a gray image into target (targetWidth x targetHeight).
    static bool resampleRegion(const uint8_t* gray, int grayWidth, int x, int y, int w, int h,
                               uint8_t* target, int targetWidth, int targetHeight);
    // Atkinson dither of 8-bit gray into a 1-bit bitmap.
    static bool dither(const uint8_t* gray, int width, int height, EpubBitmap& bitmap);
};
