#include "EpubImage.h"

#include <JPEGDEC.h>
#undef INTELSHORT
#undef INTELLONG
#undef MOTOSHORT
#undef MOTOLONG
#include <PNGdec.h>
#include <esp_heap_caps.h>
#include <memory>
#include <new>

namespace {

bool isJpeg(const uint8_t* data, size_t size) {
    return data && size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

bool isPng(const uint8_t* data, size_t size) {
    static const uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return data && size >= sizeof(signature) && memcmp(data, signature, sizeof(signature)) == 0;
}

bool dimensionsFromPngHeader(const uint8_t* data, size_t size, EpubImageInfo& info) {
    if (!isPng(data, size) || size < 24 || memcmp(data + 12, "IHDR", 4) != 0) return false;
    info.width = static_cast<int>((static_cast<uint32_t>(data[16]) << 24) |
                                  (static_cast<uint32_t>(data[17]) << 16) |
                                  (static_cast<uint32_t>(data[18]) << 8) | data[19]);
    info.height = static_cast<int>((static_cast<uint32_t>(data[20]) << 24) |
                                   (static_cast<uint32_t>(data[21]) << 16) |
                                   (static_cast<uint32_t>(data[22]) << 8) | data[23]);
    return info.width > 0 && info.height > 0;
}

bool dimensionsFromJpegHeader(const uint8_t* data, size_t size, EpubImageInfo& info) {
    if (!isJpeg(data, size)) return false;
    size_t position = 2;
    while (position + 8 < size) {
        while (position < size && data[position] != 0xFF) position++;
        while (position < size && data[position] == 0xFF) position++;
        if (position >= size) break;
        uint8_t marker = data[position++];
        if (marker == 0xD8 || marker == 0xD9 || (marker >= 0xD0 && marker <= 0xD7)) continue;
        if (position + 2 > size) break;
        uint16_t length = static_cast<uint16_t>((data[position] << 8) | data[position + 1]);
        if (length < 2 || position + length > size) break;
        bool startOfFrame = (marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
                            (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF);
        if (startOfFrame && length >= 7) {
            info.height = static_cast<int>((data[position + 3] << 8) | data[position + 4]);
            info.width = static_cast<int>((data[position + 5] << 8) | data[position + 6]);
            return info.width > 0 && info.height > 0;
        }
        position += length;
    }
    return false;
}

// True when the first frame header is a progressive one (SOF2/6/10/14).
bool jpegIsProgressive(const uint8_t* data, size_t size) {
    if (!isJpeg(data, size)) return false;
    size_t position = 2;
    while (position + 4 < size) {
        while (position < size && data[position] != 0xFF) position++;
        while (position < size && data[position] == 0xFF) position++;
        if (position >= size) break;
        uint8_t marker = data[position++];
        if (marker == 0xD8 || marker == 0xD9 || (marker >= 0xD0 && marker <= 0xD7)) continue;
        if (marker == 0xC2 || marker == 0xC6 || marker == 0xCA || marker == 0xCE) return true;
        if ((marker >= 0xC0 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) return false;
        if (position + 2 > size) break;
        uint16_t length = static_cast<uint16_t>((data[position] << 8) | data[position + 1]);
        if (length < 2) break;
        position += length;
    }
    return false;
}

void fitDimensions(int sourceWidth, int sourceHeight, int maxWidth, int maxHeight, int& width, int& height) {
    if (sourceWidth <= 0 || sourceHeight <= 0 || maxWidth <= 0 || maxHeight <= 0) {
        width = 0;
        height = 0;
        return;
    }

    width = sourceWidth;
    height = sourceHeight;
    if (width > maxWidth) {
        height = static_cast<int>((static_cast<int64_t>(height) * maxWidth) / width);
        width = maxWidth;
    }
    if (height > maxHeight) {
        width = static_cast<int>((static_cast<int64_t>(width) * maxHeight) / height);
        height = maxHeight;
    }
    width = max(1, width);
    height = max(1, height);
}

uint8_t* allocatePsram(size_t bytes, bool clear = false) {
    if (bytes == 0) return nullptr;
    uint8_t* result = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!result) result = static_cast<uint8_t*>(malloc(bytes));
    if (result && clear) memset(result, 0, bytes);
    return result;
}

int pngBytesPerPixel(int pixelType) {
    switch (pixelType) {
        case PNG_PIXEL_TRUECOLOR:
            return 3;
        case PNG_PIXEL_GRAY_ALPHA:
            return 2;
        case PNG_PIXEL_TRUECOLOR_ALPHA:
            return 4;
        default:
            return 1;
    }
}

int pngPackedRowBytes(int width, int pixelType, int bitsPerPixel) {
    if ((pixelType == PNG_PIXEL_GRAYSCALE || pixelType == PNG_PIXEL_INDEXED) && bitsPerPixel < 8) {
        return (width * bitsPerPixel + 7) / 8;
    }
    return width * pngBytesPerPixel(pixelType);
}

int requiredPngBufferBytes(int width, int pixelType, int bitsPerPixel) {
    const int pitch = pngPackedRowBytes(width, pixelType, bitsPerPixel);
    return ((pitch + 1) * 2) + 32;
}

bool isSupportedPngFormat(int pixelType, int bitsPerPixel) {
    if (pixelType == PNG_PIXEL_GRAYSCALE || pixelType == PNG_PIXEL_INDEXED) {
        return bitsPerPixel == 1 || bitsPerPixel == 2 || bitsPerPixel == 4 || bitsPerPixel == 8;
    }
    return bitsPerPixel == 8;
}

// Gray for a colour pixel. Plain luma renders saturated colours (bright red
// text on a dark background, say) almost the same gray as their surroundings.
// Saturated colours look brighter to the eye than their luma
// (Helmholtz-Kohlrausch), so the perceptual mode adds half the chroma, scaled
// by brightness: vivid highlights lift, dim tints and neutrals barely move.
inline uint8_t colorToGray(uint8_t r, uint8_t g, uint8_t b, bool perceptual) {
    const int luma = (r * 77 + g * 150 + b * 29) >> 8;
    if (!perceptual) return static_cast<uint8_t>(luma);
    const int brightest = max(r, max(g, b));
    const int chroma = brightest - min(r, min(g, b));
    return static_cast<uint8_t>(min(255, luma + ((chroma * brightest) >> 9)));
}

struct JpegContext {
    uint8_t* gray = nullptr;
    bool rgb565 = false;  // pixels arrive as RGB565 for perceptual gray
    uint8_t* intermediate = nullptr;
    int scaledSourceWidth = 0;
    int scaledSourceHeight = 0;
    int targetWidth = 0;
    int targetHeight = 0;
    uint32_t lastYield = 0;
};

int jpegDraw(JPEGDRAW* draw) {
    JpegContext* context = static_cast<JpegContext*>(draw->pUser);
    if (!context || !context->gray || !draw->pPixels) return 0;

    if (millis() - context->lastYield >= 8) {
        yield();
        context->lastYield = millis();
    }

    const uint8_t* pixels = reinterpret_cast<const uint8_t*>(draw->pPixels);
    const int sourceStride = draw->iWidth;
    const int validWidth = draw->iWidthUsed;
    if (context->intermediate) {
        int copyWidth = min(validWidth, context->scaledSourceWidth - draw->x);
        int copyHeight = min(draw->iHeight, context->scaledSourceHeight - draw->y);
        if (copyWidth <= 0 || copyHeight <= 0) return 1;
        if (context->rgb565) {
            const uint16_t* colors = reinterpret_cast<const uint16_t*>(draw->pPixels);
            for (int row = 0; row < copyHeight; row++) {
                const uint16_t* in = colors + static_cast<size_t>(row) * sourceStride;
                uint8_t* out = context->intermediate + static_cast<size_t>(draw->y + row) * context->scaledSourceWidth + draw->x;
                for (int x = 0; x < copyWidth; x++) {
                    const uint16_t c = in[x];
                    const uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
                    const uint8_t g = ((c >> 5) & 0x3F) * 255 / 63;
                    const uint8_t b = (c & 0x1F) * 255 / 31;
                    out[x] = colorToGray(r, g, b, true);
                }
            }
            return 1;
        }
        for (int row = 0; row < copyHeight; row++) {
            memcpy(context->intermediate + static_cast<size_t>(draw->y + row) * context->scaledSourceWidth + draw->x,
                   pixels + static_cast<size_t>(row) * sourceStride, copyWidth);
        }
        return 1;
    }

    const int sourceEndX = draw->x + validWidth;
    const int sourceEndY = draw->y + draw->iHeight;
    int targetStartX = static_cast<int>((static_cast<int64_t>(draw->x) * context->targetWidth) /
                                        context->scaledSourceWidth);
    int targetEndX = sourceEndX >= context->scaledSourceWidth
                         ? context->targetWidth
                         : static_cast<int>((static_cast<int64_t>(sourceEndX) * context->targetWidth) /
                                            context->scaledSourceWidth);
    int targetStartY = static_cast<int>((static_cast<int64_t>(draw->y) * context->targetHeight) /
                                        context->scaledSourceHeight);
    int targetEndY = sourceEndY >= context->scaledSourceHeight
                         ? context->targetHeight
                         : static_cast<int>((static_cast<int64_t>(sourceEndY) * context->targetHeight) /
                                            context->scaledSourceHeight);

    targetStartX = constrain(targetStartX, 0, context->targetWidth);
    targetEndX = constrain(targetEndX, 0, context->targetWidth);
    targetStartY = constrain(targetStartY, 0, context->targetHeight);
    targetEndY = constrain(targetEndY, 0, context->targetHeight);

    for (int targetY = targetStartY; targetY < targetEndY; targetY++) {
        int sourceY = static_cast<int>((static_cast<int64_t>(targetY) * context->scaledSourceHeight) /
                                       context->targetHeight) - draw->y;
        sourceY = constrain(sourceY, 0, draw->iHeight - 1);
        const uint8_t* sourceRow = pixels + sourceY * sourceStride;
        uint8_t* targetRow = context->gray + targetY * context->targetWidth;
        for (int targetX = targetStartX; targetX < targetEndX; targetX++) {
            int sourceX = static_cast<int>((static_cast<int64_t>(targetX) * context->scaledSourceWidth) /
                                           context->targetWidth) - draw->x;
            sourceX = constrain(sourceX, 0, validWidth - 1);
            targetRow[targetX] = sourceRow[sourceX];
        }
    }
    return 1;
}

// Resamples one row or column. Shrinking averages every source pixel by the
// fraction of it each output pixel covers, so detail turns into the right
// shade of gray instead of aliasing; enlarging interpolates linearly.
void resampleLine(const uint8_t* source, int sourceLength, int sourceStep,
                  uint8_t* target, int targetLength, int targetStep) {
    if (targetLength >= sourceLength) {
        for (int i = 0; i < targetLength; i++) {
            uint32_t position = targetLength > 1
                                    ? static_cast<uint32_t>((static_cast<uint64_t>(i) * (sourceLength - 1) << 16) /
                                                            (targetLength - 1))
                                    : 0;
            int index0 = position >> 16;
            int index1 = min(index0 + 1, sourceLength - 1);
            uint32_t fraction = position & 0xFFFF;
            target[i * targetStep] = static_cast<uint8_t>(
                (source[index0 * sourceStep] * (65536U - fraction) + source[index1 * sourceStep] * fraction) >> 16);
        }
        return;
    }

    for (int i = 0; i < targetLength; i++) {
        uint64_t start = (static_cast<uint64_t>(i) * sourceLength << 16) / targetLength;
        uint64_t end = (static_cast<uint64_t>(i + 1) * sourceLength << 16) / targetLength;
        uint64_t sum = 0;
        uint64_t position = start;
        int index = static_cast<int>(start >> 16);
        while (position < end && index < sourceLength) {
            uint64_t next = min(static_cast<uint64_t>(index + 1) << 16, end);
            sum += static_cast<uint64_t>(source[index * sourceStep]) * (next - position);
            position = next;
            index++;
        }
        target[i * targetStep] = static_cast<uint8_t>((sum + (end - start) / 2) / (end - start));
    }
}

void resampleGray(const uint8_t* source, int sourceWidth, int sourceHeight,
                  uint8_t* target, int targetWidth, int targetHeight) {
    if (!source || !target || sourceWidth <= 0 || sourceHeight <= 0 || targetWidth <= 0 || targetHeight <= 0) return;
    if (sourceWidth == targetWidth && sourceHeight == targetHeight) {
        memcpy(target, source, static_cast<size_t>(targetWidth) * targetHeight);
        return;
    }

    // Separable: rows into a sourceHeight x targetWidth buffer, then columns.
    uint8_t* rows = allocatePsram(static_cast<size_t>(sourceHeight) * targetWidth);
    if (!rows) {
        for (int y = 0; y < targetHeight; y++) {
            const uint8_t* sourceRow = source + static_cast<size_t>(y * sourceHeight / targetHeight) * sourceWidth;
            for (int x = 0; x < targetWidth; x++) target[y * targetWidth + x] = sourceRow[x * sourceWidth / targetWidth];
        }
        return;
    }
    for (int y = 0; y < sourceHeight; y++) {
        resampleLine(source + static_cast<size_t>(y) * sourceWidth, sourceWidth, 1,
                     rows + static_cast<size_t>(y) * targetWidth, targetWidth, 1);
        if ((y & 31) == 0) yield();
    }
    for (int x = 0; x < targetWidth; x++) {
        resampleLine(rows + x, sourceHeight, targetWidth, target + x, targetHeight, targetWidth);
    }
    free(rows);
}

int chooseJpegScale(int sourceWidth, int sourceHeight, int targetWidth, int targetHeight, int& denominator) {
    int64_t targetScaleX = (static_cast<int64_t>(targetWidth) * 1000) / sourceWidth;
    int64_t targetScaleY = (static_cast<int64_t>(targetHeight) * 1000) / sourceHeight;
    int64_t targetScale = min(targetScaleX, targetScaleY);
    if (targetScale <= 125) {
        denominator = 8;
        return JPEG_SCALE_EIGHTH;
    }
    if (targetScale <= 250) {
        denominator = 4;
        return JPEG_SCALE_QUARTER;
    }
    if (targetScale <= 500) {
        denominator = 2;
        return JPEG_SCALE_HALF;
    }
    denominator = 1;
    return 0;
}

struct PngContext {
    PNG* decoder = nullptr;
    uint8_t* gray = nullptr;
    bool perceptual = false;  // see colorToGray()
    uint8_t* sourceLine = nullptr;
    uint8_t* rows = nullptr;  // sourceHeight x targetWidth, for area resampling
    int sourceWidth = 0;
    int sourceHeight = 0;
    int targetWidth = 0;
    int targetHeight = 0;
    int lastTargetRow = -1;
    uint32_t lastYield = 0;
};

uint8_t readPackedSample(const uint8_t* pixels, int x, int bitsPerSample) {
    if (bitsPerSample == 8) return pixels[x];
    int bitOffset = x * bitsPerSample;
    int shift = 8 - bitsPerSample - (bitOffset & 7);
    uint8_t mask = static_cast<uint8_t>((1U << bitsPerSample) - 1U);
    return static_cast<uint8_t>((pixels[bitOffset >> 3] >> shift) & mask);
}

uint8_t expandSample(uint8_t sample, int bitsPerSample) {
    if (bitsPerSample == 8) return sample;
    return static_cast<uint8_t>((sample * 255U) / ((1U << bitsPerSample) - 1U));
}

void pngLineToGray(PNGDRAW* draw, uint8_t* output, int width, uint32_t transparentColor, bool perceptual) {
    const uint8_t* pixels = draw->pPixels;
    int bits = draw->iBpp;
    switch (draw->iPixelType) {
        case PNG_PIXEL_GRAYSCALE:
            for (int x = 0; x < width; x++) {
                uint8_t sample = readPackedSample(pixels, x, bits);
                output[x] = (draw->iHasAlpha && sample == static_cast<uint8_t>(transparentColor))
                                ? 255
                                : expandSample(sample, bits);
            }
            break;
        case PNG_PIXEL_TRUECOLOR:
            for (int x = 0; x < width; x++) {
                const uint8_t* pixel = pixels + x * 3;
                uint32_t color = (static_cast<uint32_t>(pixel[0]) << 16) |
                                 (static_cast<uint32_t>(pixel[1]) << 8) | pixel[2];
                output[x] = (draw->iHasAlpha && color == transparentColor)
                                ? 255
                                : colorToGray(pixel[0], pixel[1], pixel[2], perceptual);
            }
            break;
        case PNG_PIXEL_INDEXED:
            for (int x = 0; x < width; x++) {
                uint8_t index = readPackedSample(pixels, x, bits);
                uint8_t* color = draw->pPalette ? draw->pPalette + index * 3 : nullptr;
                uint8_t alpha = (draw->iHasAlpha && draw->pPalette) ? draw->pPalette[768 + index] : 255;
                output[x] = !color || alpha < 128
                                ? 255
                                : colorToGray(color[0], color[1], color[2], perceptual);
            }
            break;
        case PNG_PIXEL_GRAY_ALPHA:
            for (int x = 0; x < width; x++) {
                uint8_t gray = pixels[x * 2];
                uint8_t alpha = pixels[x * 2 + 1];
                output[x] = static_cast<uint8_t>((gray * alpha + 255U * (255U - alpha)) / 255U);
            }
            break;
        case PNG_PIXEL_TRUECOLOR_ALPHA:
            for (int x = 0; x < width; x++) {
                const uint8_t* pixel = pixels + x * 4;
                uint8_t gray = colorToGray(pixel[0], pixel[1], pixel[2], perceptual);
                uint8_t alpha = pixel[3];
                output[x] = static_cast<uint8_t>((gray * alpha + 255U * (255U - alpha)) / 255U);
            }
            break;
        default:
            memset(output, 255, width);
            break;
    }
}

int pngDraw(PNGDRAW* draw) {
    PngContext* context = static_cast<PngContext*>(draw->pUser);
    if (!context || !context->gray || !context->sourceLine) return 0;

    if (millis() - context->lastYield >= 8) {
        yield();
        context->lastYield = millis();
    }

    uint32_t transparent = context->decoder ? context->decoder->getTransparentColor() : 0;
    pngLineToGray(draw, context->sourceLine, context->sourceWidth, transparent, context->perceptual);

    if (context->rows) {
        if (draw->y >= 0 && draw->y < context->sourceHeight) {
            resampleLine(context->sourceLine, context->sourceWidth, 1,
                         context->rows + static_cast<size_t>(draw->y) * context->targetWidth,
                         context->targetWidth, 1);
        }
        return 1;
    }

    int targetStartY = (draw->y * context->targetHeight) / context->sourceHeight;
    int targetEndY = ((draw->y + 1) * context->targetHeight) / context->sourceHeight;
    if (targetEndY <= targetStartY) targetEndY = targetStartY + 1;
    if (targetStartY <= context->lastTargetRow) targetStartY = context->lastTargetRow + 1;
    targetEndY = min(targetEndY, context->targetHeight);

    for (int targetY = targetStartY; targetY < targetEndY; targetY++) {
        uint8_t* targetRow = context->gray + targetY * context->targetWidth;
        for (int targetX = 0; targetX < context->targetWidth; targetX++) {
            int sourceX = static_cast<int>((static_cast<int64_t>(targetX) * context->sourceWidth) /
                                           context->targetWidth);
            targetRow[targetX] = context->sourceLine[min(sourceX, context->sourceWidth - 1)];
        }
        context->lastTargetRow = targetY;
    }
    return 1;
}

bool decodeJpeg(const uint8_t* data, size_t size, uint8_t* gray, int targetWidth, int targetHeight,
                EpubImageInfo& sourceInfo, bool perceptual) {
    std::unique_ptr<JPEGDEC> jpeg(new (std::nothrow) JPEGDEC());
    if (!jpeg || jpeg->openRAM(const_cast<uint8_t*>(data), static_cast<int>(size), jpegDraw) != 1) return false;

    sourceInfo.width = jpeg->getWidth();
    sourceInfo.height = jpeg->getHeight();
    bool progressive = jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE;
    int denominator = 1;
    int scale = chooseJpegScale(sourceInfo.width, sourceInfo.height, targetWidth, targetHeight, denominator);
    if (progressive) {
        scale = JPEG_SCALE_EIGHTH;
        denominator = 8;
    }

    JpegContext context;
    context.gray = gray;
    context.scaledSourceWidth = (sourceInfo.width + denominator - 1) / denominator;
    context.scaledSourceHeight = (sourceInfo.height + denominator - 1) / denominator;
    context.targetWidth = targetWidth;
    context.targetHeight = targetHeight;
    context.lastYield = millis();

    // Decode at the DCT-scaled size, then area-average down to the target.
    // Sampling straight into the target skipped most source pixels.
    size_t intermediateBytes = static_cast<size_t>(context.scaledSourceWidth) * context.scaledSourceHeight;
    if (intermediateBytes <= 2 * 1024 * 1024 &&
        intermediateBytes + (256 * 1024) < heap_caps_get_free_size(MALLOC_CAP_SPIRAM)) {
        context.intermediate = allocatePsram(intermediateBytes);
        if (context.intermediate) memset(context.intermediate, 255, intermediateBytes);
    }

    // Colour output only goes through the full-size intermediate buffer.
    context.rgb565 = perceptual && context.intermediate;
    jpeg->setPixelType(context.rgb565 ? RGB565_LITTLE_ENDIAN : EIGHT_BIT_GRAYSCALE);
    jpeg->setUserPointer(&context);
    int result = jpeg->decode(0, 0, scale);
    jpeg->close();
    if (result == 1 && context.intermediate) {
        resampleGray(context.intermediate, context.scaledSourceWidth, context.scaledSourceHeight,
                     gray, targetWidth, targetHeight);
    }
    if (context.intermediate) free(context.intermediate);
    return result == 1;
}

bool decodePng(const uint8_t* data, size_t size, uint8_t* gray, int targetWidth, int targetHeight,
               EpubImageInfo& sourceInfo, bool perceptual) {
    std::unique_ptr<PNG> png(new (std::nothrow) PNG());
    if (!png || png->openRAM(const_cast<uint8_t*>(data), static_cast<int>(size), pngDraw) != PNG_SUCCESS) return false;

    sourceInfo.width = png->getWidth();
    sourceInfo.height = png->getHeight();
    if (sourceInfo.width <= 0 || sourceInfo.height <= 0 || sourceInfo.width > 8192) {
        png->close();
        return false;
    }
    const int pixelType = png->getPixelType();
    const int bitsPerPixel = png->getBpp();
    const int requiredBuffer = requiredPngBufferBytes(sourceInfo.width, pixelType, bitsPerPixel);
    if (!isSupportedPngFormat(pixelType, bitsPerPixel) ||
        requiredBuffer > PNG_MAX_BUFFERED_PIXELS) {
        Serial.printf("EpubImage: Unsupported PNG format or scanline buffer (%d bytes)\n", requiredBuffer);
        png->close();
        return false;
    }

    uint8_t* sourceLine = allocatePsram(sourceInfo.width);
    if (!sourceLine) {
        png->close();
        return false;
    }

    PngContext context;
    context.decoder = png.get();
    context.perceptual = perceptual;
    context.gray = gray;
    context.sourceLine = sourceLine;
    context.sourceWidth = sourceInfo.width;
    context.sourceHeight = sourceInfo.height;
    context.targetWidth = targetWidth;
    context.targetHeight = targetHeight;
    context.lastYield = millis();
    // Without room for the row buffer, fall back to sampling into the target.
    size_t rowsBytes = static_cast<size_t>(sourceInfo.height) * targetWidth;
    if (rowsBytes <= 2 * 1024 * 1024) {
        context.rows = allocatePsram(rowsBytes);
        if (context.rows) memset(context.rows, 255, rowsBytes);
    }
    int result = png->decode(&context, 0);
    if (result == PNG_SUCCESS && context.rows) {
        for (int x = 0; x < targetWidth; x++) {
            resampleLine(context.rows + x, sourceInfo.height, targetWidth, gray + x, targetHeight, targetWidth);
        }
    }
    if (context.rows) free(context.rows);
    free(sourceLine);
    png->close();
    return result == PNG_SUCCESS;
}

// Stretches the darkest and lightest 1% of pixels to black and white so
// washed-out scans use the full range. Gain is capped at 2x so nearly flat
// images are not turned into noise.
void autoLevels(uint8_t* gray, int width, int height) {
    uint32_t histogram[256] = {};
    const size_t count = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < count; i++) histogram[gray[i]]++;
    const size_t clip = count / 100;
    int low = 0, high = 255;
    size_t seen = 0;
    while (low < 255 && (seen += histogram[low]) <= clip) low++;
    seen = 0;
    while (high > 0 && (seen += histogram[high]) <= clip) high--;
    if (high - low < 24) return;
    if (high - low < 128) {
        int center = (low + high) / 2;
        low = max(0, center - 64);
        high = min(255, center + 64);
    }
    if (low == 0 && high == 255) return;
    uint8_t table[256];
    for (int v = 0; v < 256; v++) table[v] = static_cast<uint8_t>(constrain((v - low) * 255 / (high - low), 0, 255));
    for (size_t i = 0; i < count; i++) gray[i] = table[gray[i]];
}

// Mild unsharp mask (3x3 box, amount 0.5) to restore edges softened by
// averaging a large cover down to a thumbnail.
void sharpen(uint8_t* gray, int width, int height) {
    if (width < 3 || height < 3) return;
    uint8_t* source = allocatePsram(static_cast<size_t>(width) * height);
    if (!source) return;
    memcpy(source, gray, static_cast<size_t>(width) * height);
    for (int y = 0; y < height; y++) {
        const int y0 = max(0, y - 1), y1 = min(height - 1, y + 1);
        for (int x = 0; x < width; x++) {
            const int x0 = max(0, x - 1), x1 = min(width - 1, x + 1);
            int sum = 0;
            for (int yy : {y0, y, y1})
                for (int xx : {x0, x, x1}) sum += source[yy * width + xx];
            const int value = source[y * width + x];
            gray[y * width + x] = static_cast<uint8_t>(constrain(value + (value - sum / 9) / 2, 0, 255));
        }
    }
    free(source);
}

bool ditherAtkinson(const uint8_t* gray, int width, int height, EpubBitmap& bitmap) {
    if (!bitmap.allocate(width, height)) return false;

    size_t errorBytes = static_cast<size_t>(width + 4) * sizeof(int16_t);
    int16_t* current = reinterpret_cast<int16_t*>(allocatePsram(errorBytes, true));
    int16_t* next = reinterpret_cast<int16_t*>(allocatePsram(errorBytes, true));
    int16_t* afterNext = reinterpret_cast<int16_t*>(allocatePsram(errorBytes, true));
    bool useAtkinson = current && next && afterNext;

    static const uint8_t bayer4x4[16] = {0, 128, 32, 160, 192, 64, 224, 96,
                                         48, 176, 16, 144, 240, 112, 208, 80};
    for (int y = 0; y < height; y++) {
        uint8_t* output = bitmap.pixels() + y * bitmap.stride();
        const uint8_t* input = gray + y * width;
        for (int x = 0; x < width; x++) {
            int value = ((static_cast<int>(input[x]) - 128) * 108) / 100 + 128;
            value = constrain(value, 0, 255);
            bool white;
            if (useAtkinson) {
                int adjusted = constrain(value + current[x + 2], 0, 255);
                white = adjusted >= 128;
                int error = (adjusted - (white ? 255 : 0)) >> 3;
                current[x + 3] += error;
                current[x + 4] += error;
                next[x + 1] += error;
                next[x + 2] += error;
                next[x + 3] += error;
                afterNext[x + 2] += error;
            } else {
                white = value >= bayer4x4[((y & 3) << 2) | (x & 3)];
            }
            if (!white) output[x >> 3] |= static_cast<uint8_t>(0x80 >> (x & 7));
        }
        if (useAtkinson) {
            int16_t* oldCurrent = current;
            current = next;
            next = afterNext;
            afterNext = oldCurrent;
            memset(afterNext, 0, errorBytes);
        }
        if ((y & 15) == 0) yield();
    }

    if (current) free(current);
    if (next) free(next);
    if (afterNext) free(afterNext);
    return true;
}

// Serpentine Floyd-Steinberg for full-screen covers. Unlike Atkinson it keeps
// all of the error, so large smooth areas keep their tone instead of
// collapsing to black or white; alternating direction avoids diagonal worms.
bool ditherFloydSteinberg(const uint8_t* gray, int width, int height, EpubBitmap& bitmap) {
    if (!bitmap.allocate(width, height)) return false;
    size_t errorBytes = static_cast<size_t>(width + 2) * sizeof(int16_t);
    int16_t* current = reinterpret_cast<int16_t*>(allocatePsram(errorBytes, true));
    int16_t* next = reinterpret_cast<int16_t*>(allocatePsram(errorBytes, true));
    if (!current || !next) {
        free(current);
        free(next);
        return false;
    }
    for (int y = 0; y < height; y++) {
        uint8_t* output = bitmap.pixels() + y * bitmap.stride();
        const uint8_t* input = gray + y * width;
        const bool leftToRight = (y & 1) == 0;
        const int step = leftToRight ? 1 : -1;
        for (int i = 0; i < width; i++) {
            const int x = leftToRight ? i : width - 1 - i;
            const int value = constrain(input[x] + current[x + 1] / 16, -64, 319);
            const bool white = value >= 128;
            const int error = value - (white ? 255 : 0);
            current[x + 1 + step] += error * 7;
            next[x + 1 - step] += error * 3;
            next[x + 1] += error * 5;
            next[x + 1 + step] += error;
            if (!white) output[x >> 3] |= static_cast<uint8_t>(0x80 >> (x & 7));
        }
        int16_t* finished = current;
        current = next;
        next = finished;
        memset(next, 0, errorBytes);
        if ((y & 15) == 0) yield();
    }
    free(current);
    free(next);
    return true;
}

}  // namespace

EpubBitmap::~EpubBitmap() { clear(); }

EpubBitmap::EpubBitmap(EpubBitmap&& other) noexcept {
    _pixels = other._pixels;
    _stride = other._stride;
    width = other.width;
    height = other.height;
    other._pixels = nullptr;
    other._stride = 0;
    other.width = 0;
    other.height = 0;
}

EpubBitmap& EpubBitmap::operator=(EpubBitmap&& other) noexcept {
    if (this == &other) return *this;
    clear();
    _pixels = other._pixels;
    _stride = other._stride;
    width = other.width;
    height = other.height;
    other._pixels = nullptr;
    other._stride = 0;
    other.width = 0;
    other.height = 0;
    return *this;
}

void EpubBitmap::clear() {
    if (_pixels) free(_pixels);
    _pixels = nullptr;
    _stride = 0;
    width = 0;
    height = 0;
}

bool EpubBitmap::allocate(int bitmapWidth, int bitmapHeight) {
    clear();
    if (bitmapWidth <= 0 || bitmapHeight <= 0) return false;
    width = bitmapWidth;
    height = bitmapHeight;
    _stride = (width + 7) / 8;
    _pixels = allocatePsram(static_cast<size_t>(_stride) * height, true);
    if (_pixels) return true;
    clear();
    return false;
}

void EpubBitmap::draw(Book32Display& display, int x, int y) const {
    if (valid()) display.drawBitmap(x, y, _pixels, width, height, GxEPD_BLACK);
}

bool EpubImageDecoder::dimensions(const uint8_t* data, size_t size, EpubImageInfo& info) {
    info = {};
    if (dimensionsFromPngHeader(data, size, info) || dimensionsFromJpegHeader(data, size, info)) return true;
    if (isJpeg(data, size)) {
        std::unique_ptr<JPEGDEC> jpeg(new (std::nothrow) JPEGDEC());
        if (!jpeg || jpeg->openRAM(const_cast<uint8_t*>(data), static_cast<int>(size), nullptr) != 1) return false;
        info.width = jpeg->getWidth();
        info.height = jpeg->getHeight();
        jpeg->close();
    } else if (isPng(data, size)) {
        std::unique_ptr<PNG> png(new (std::nothrow) PNG());
        if (!png || png->openRAM(const_cast<uint8_t*>(data), static_cast<int>(size), nullptr) != PNG_SUCCESS) return false;
        info.width = png->getWidth();
        info.height = png->getHeight();
        png->close();
    }
    return info.width > 0 && info.height > 0;
}

bool EpubImageDecoder::decode(const uint8_t* data, size_t size, int maxWidth, int maxHeight, EpubBitmap& bitmap,
                              EpubImageQuality quality) {
    const bool cover = quality == EpubImageQuality::Cover;
    if (cover && jpegIsProgressive(data, size)) {
        Serial.println("EpubImage: progressive JPEG cover not decoded (known JPEGDEC overrun)");
        return false;
    }
    EpubImageInfo source;
    if (!dimensions(data, size, source)) return false;

    int targetWidth = 0;
    int targetHeight = 0;
    fitDimensions(source.width, source.height, maxWidth, maxHeight, targetWidth, targetHeight);
    if (targetWidth <= 0 || targetHeight <= 0) return false;

    size_t grayBytes = static_cast<size_t>(targetWidth) * targetHeight;
    uint8_t* gray = allocatePsram(grayBytes);
    if (!gray) return false;
    memset(gray, 255, grayBytes);

    bool decoded = false;
    if (isJpeg(data, size)) decoded = decodeJpeg(data, size, gray, targetWidth, targetHeight, source, cover);
    else if (isPng(data, size)) decoded = decodePng(data, size, gray, targetWidth, targetHeight, source, cover);

    bool dithered = false;
    if (decoded) {
        autoLevels(gray, targetWidth, targetHeight);
        // Only images shrunk by more than 1.25x lose enough edge detail to need it.
        if (source.width * 4 > targetWidth * 5) sharpen(gray, targetWidth, targetHeight);
        dithered = cover ? ditherFloydSteinberg(gray, targetWidth, targetHeight, bitmap)
                         : ditherAtkinson(gray, targetWidth, targetHeight, bitmap);
    }
    free(gray);
    return dithered;
}
