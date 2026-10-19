# InkDeck on the plain LILYGO T5-4.7 E-Paper S3 (V2.3 / V2.4)

Experimental port for the LILYGO board sold as **T5-4.7 E-Paper S3** (GitHub:
LilyGo-EPD47, `esp32s3` branch; silkscreen `Screen-4.7-S3 V2.4`). It is the
model **without** frontlight, GPS, LoRa or a battery gauge. It is not the
T5 E-Paper S3 Pro / Pro Lite (H752-01) that the `lilygo_t5s3_pro` environment
targets, and the two firmware images are not interchangeable.

## What is different from the Pro

| | Pro / Pro Lite (H752-01) | Plain T5-4.7 S3 |
| --- | --- | --- |
| Panel power and control | TPS65185 + PCA9535 (EPDiy V7 mapping) | 74HCT4094 shift register on GPIO13 / 12 / 0 |
| Panel data bus | D0..D7 = 5,6,7,15,16,17,18,8 | D0..D7 = 8,1,2,3,4,5,6,7; CKV 38, STH 40, CKH 41 |
| Touch (GT911) | SDA 39, SCL 40, INT 3, RST 9 | SDA 18, SCL 17, INT 47, no reset line |
| MicroSD | CS 12, MISO 21, SCK 14, MOSI 13 | CS 42, MISO 16, SCK 11, MOSI 15 |
| Battery | BQ27220 gauge over I2C | VBAT / 2 on GPIO14 (ADC), charging inferred from the voltage trend |
| User button | BOOT (GPIO0) | Side key on GPIO21; GPIO0 is the shift register strobe |
| Frontlight | PT4103 on GPIO11 | None fitted; optional add-on on GPIO10 (see below) |
| Inkade video | Queued DMA transport | Four-pass partial updates only (LE is a register bit, not a GPIO) |

The shift register protocol (bit order, power-up and power-down sequencing)
is the one LILYGO's own `ed047tc1.c` uses, implemented in FastEPD by the
`LilyGoEinkPower` / `LilyGoIOInit` / `LilyGoRowControl` callbacks. This port
adds a panel definition, `BB_PANEL_LILYGO_T5S3`, with the EPD47 pins and reuses
those callbacks unchanged.

## Building

```
pio run -e lilygo_t5s3 -t upload
pio run -e lilygo_t5s3 -t uploadfs
```

The second command writes the web interface (the 1 MB `spiffs` partition);
without it the device boots but the web pages are missing. Same partition
layout as the Pro build (`partitions_16MB_lilygo.csv`), so later updates keep
the internal book storage.

## Controls

- Tap icons and books; while reading, tap the left or right third of the page.
- Side key (GPIO21): press = back, double click = the "side" action from
  Settings > Light & buttons (light on/off by default), hold 2 s and release =
  sleep, press again = wake.
- BOOT and RESET are the usual ESP32 strapping buttons. BOOT is also the
  shift register's store line, so it must stay free.

## Optional frontlight

The board has no light. If you add an LED strip with a driver (for example a
PT4103 or any enable/PWM-controlled constant-current driver), connect its
control input to **GPIO10**, which is the unused MOSI pad of the 40-pin
header. InkDeck drives it with 500 Hz PWM at the brightness chosen in
Settings, holds it low in deep sleep, and the settings page, the web UI and the
side key's double click all work as on the Pro Lite. Another free GPIO can be
used instead by changing `PIN_FRONTLIGHT` in `include/Config.h`.

## Known gaps and tuning

- Untested on hardware at the time of writing; the row timing in
  `LilyGoRowControl` was written for the older H752 Pro, which shares this
  panel and register layout. Ghosting or faint stripes would be tuned there
  first, then in the gray pulse tables (`StickyGrayImage::LILYGO_*`).
- No front capacitive Home key: Home is available from a Bluetooth gamepad
  and by returning through the menus.
- Battery percentage is derived from voltage. `BATTERY_VOLTAGE_CALIBRATION`
  in `Config.h` corrects the ADC reading against a multimeter.
- OTA from GitHub Releases looks for `inkdeck-lilygo-t5s3-firmware.bin` and
  `inkdeck-lilygo-t5s3-littlefs.bin`; until releases carry those assets,
  update over USB.
