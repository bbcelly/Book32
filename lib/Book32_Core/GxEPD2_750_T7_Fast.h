// Display Library for SPI e-paper panels from Dalian Good Display and boards from Waveshare.
// Requires HW SPI and Adafruit_GFX. Caution: the e-paper panels require 3.3V supply AND data lines!
//
// based on Demo Example from Good Display: 
// Panel: GDEW075T7 : http://www.e-paper-display.com/products_detail/productId=456.html
// Controller: GD7965 : http://www.e-paper-display.com/download_detail/downloadsId=821.html
//
// Author: Jean-Marc Zingg
//
// Version: see library.properties
//
// Library: https://github.com/ZinggJM/GxEPD2
//
// Modified for InkDeck from GxEPD2 1.6.7 GxEPD2_750_T7 (GPL-3.0): renamed,
// with shorter partial-update LUT phases for faster Book32 page turns and a
// forced-temperature fast full update (as in GxEPD2_750_GDEY075T7).

#ifndef _GXEPD2_750_T7_FAST_H_
#define _GXEPD2_750_T7_FAST_H_

#include <GxEPD2_EPD.h>

class GxEPD2_750_T7_Fast : public GxEPD2_EPD
{
  public:
    // attributes
    static const uint16_t WIDTH = 800;
    static const uint16_t WIDTH_VISIBLE = WIDTH;
    static const uint16_t HEIGHT = 480;
    static const GxEPD2::Panel panel = GxEPD2::GDEW075T7;
    static const bool hasColor = false;
    static const bool hasPartialUpdate = true;
    static const bool usePartialUpdateWindow = false; // set false for better image
    static const bool hasFastPartialUpdate = true; // set this false to force full refresh always
    static const uint16_t power_on_time = 140; // ms, e.g. 134460us
    static const uint16_t power_off_time = 42; // ms, e.g. 40033us
    static const bool useFastFullUpdate = true; // forced-temperature OTP waveform
    static const uint16_t full_refresh_time = 1600; // ms, fast full update measured on Book32 (4200 from the sensor)
    static const uint16_t partial_refresh_time = 950; // ms, shortened InkDeck partial waveform
    // constructor
    GxEPD2_750_T7_Fast(int16_t cs, int16_t dc, int16_t rst, int16_t busy);
    // methods (virtual)
    //  Support for Bitmaps (Sprites) to Controller Buffer and to Screen
    void clearScreen(uint8_t value = 0xFF); // init controller memory and screen (default white)
    void writeScreenBuffer(uint8_t value = 0xFF); // init controller memory (default white)
    // write to controller memory, without screen refresh; x and w should be multiple of 8
    void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // for differential update: set current and previous buffers equal (for fast partial update to work correctly)
    // done by controller (N2OCP); override with empty code
    void writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false) {};
    void writeImagePartAgain(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                             int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false) {};
    // write sprite of native data to controller memory, without screen refresh; x and w should be multiple of 8
    void writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write to controller memory, with screen refresh; x and w should be multiple of 8
    void drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write sprite of native data to controller memory, with screen refresh; x and w should be multiple of 8
    void drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void refresh(bool partial_update_mode = false); // screen refresh from controller memory to full screen
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h); // screen refresh from controller memory, partial screen
    void powerOff(); // turns off generation of panel driving voltages, avoids screen fading over time
    void hibernate(); // turns powerOff() and sets controller to deep sleep for minimum power use, ONLY if wakeable by RST (rst >= 0)
    // InkDeck: per-pixel pulse drive (grayscale sleep covers). After a full
    // refresh, load a one-phase waveform: old/new K->W pulses white, W->K
    // pulses black, unchanged pixels are held. Each scan writes both planes of
    // a window (rows of w/8 bytes, x and w multiples of 8) and refreshes it.
    void pulseBegin(uint8_t frames);
    void pulseScan(const uint8_t* oldPlane, const uint8_t* newPlane, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    // Back to the normal waveforms on the next refresh.
    void pulseEnd();
  private:
    void _writePulseLut(uint8_t command, uint8_t voltages, uint8_t frames);
    void _writePlaneRows(uint8_t command, const uint8_t* plane, uint16_t rowBytes, uint16_t rows);
    void _setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void _PowerOn();
    void _PowerOff();
    void _InitDisplay();
    void _Init_Full();
    void _Init_Part();
    void _Update_Full();
    void _Update_Part();
  private:
    static const unsigned char lut_20_LUTC_partial[];
    static const unsigned char lut_21_LUTWW_partial[];
    static const unsigned char lut_22_LUTKW_partial[];
    static const unsigned char lut_23_LUTWK_partial[];
    static const unsigned char lut_24_LUTKK_partial[];
    static const unsigned char lut_25_LUTBD_partial[];
};

#endif
