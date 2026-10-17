# InkDeck

InkDeck is firmware for ESP32-S3 e-paper devices. It turns them into an
eReader with a few everyday apps, managed from a web page on your local
network.

Install it from your browser with the
**[InkDeck Browser Installer](https://rolohaun.github.io/Book32/)**.

## Supported devices

| Device | Display | Controls | Book storage |
| --- | --- | --- | --- |
| Book32 (DIY build) | 7.5 inch, 800 × 480 | One button | 10 MB internal |
| Seeed Studio Sticky | 3.97 inch, 800 × 480 | Touch screen and three buttons | MicroSD, or 23 MB internal without a card |
| LILYGO T5 E-Paper S3 Pro H752-01 / Pro Lite (experimental) | 4.7 inch, 960 × 540, frontlight | Touch screen and buttons | MicroSD, or internal without a card |

The LILYGO build supports the H752-01 / Pro Lite revision only, not the older
H752 board.

**Raspberry Pi:** [InkDeck for Raspberry Pi](pi/README.md) (formerly PiBook)
is a separate Python edition for a Raspberry Pi Zero 2 W with the Waveshare
7.5 inch e-Paper HAT. It is complete and installs from this repository; it is
not updated with the ESP32 releases.

## Features

- **eReader** for EPUB books, with illustrations, cover thumbnails and saved
  reading position.
- **Inkade** game player (Sticky and LILYGO): Game Boy and Game Boy Color on
  both, plus NES on the LILYGO. No games are included.
- **Klipper monitor** for 3D printers on your network.
- **Todo list.**
- **Web interface** for uploading books and games, changing settings and
  updating the firmware.
- **Sleep screen** showing the cover of the book you are reading in grayscale,
  or a message of your choice.
- **Bluetooth LE gamepads** for menus, the eReader and Inkade.
- **Updates over Wi-Fi** from the releases on this repository.

## Installing

1. Open the [InkDeck Browser Installer](https://rolohaun.github.io/Book32/) in
   desktop Chrome or Edge.
2. Choose your device.
3. Connect it with a USB cable that carries data, and follow the steps.

Updating keeps your Wi-Fi settings, books and reading progress. The installer
never formats the MicroSD card or Book32's internal book storage. If it offers
an **Erase device** option, leave that unchecked on a device that has books on
it.

## First start

**Sticky and LILYGO:** tap the **Wi-Fi** icon, pick your network, type its
password on the on-screen keyboard and tap **Connect**.

**Book32:** connect a phone or computer to the `InkDeck-Setup` Wi-Fi network.
If the setup page does not open on its own, browse to `192.168.4.1`. Choose your
network and enter its password.

Once connected, the device shows its IP address on the main menu. Open
`http://<that address>/` in a browser to upload books, add games and change
settings.

## Controls

**Book32**

- Press the button to move to the next item or page.
- Hold it to open, select or go back, depending on the screen.

**Seeed Studio Sticky**

- Tap an icon or a book to open it.
- While reading, tap the left or right third of the page to turn back or
  forward.
- The up and down buttons move back and forward; the power button selects.

**LILYGO**

- Tap an icon or a book to open it. While reading, tap the left or right third
  of the page to turn back or forward.
- The BOOT button goes back. To sleep, hold it for 2 seconds and release; press
  it again to wake.
- The front and side buttons switch the frontlight on and off. Either can be
  set to Home or Back in Settings.

**Bluetooth gamepad (all devices)**

Pair a Bluetooth LE gamepad under **Settings** on the device or in the web
interface. Put the gamepad in its BLE pairing mode first. Gamepads that only
use Classic Bluetooth cannot connect, because the ESP32-S3 does not support
it. The 8BitDo Ultimate 2C Wireless works in its Bluetooth mode.

## Games (Inkade)

Upload `.gb` and `.gbc` games (and `.nes` on the LILYGO) from the web
interface, or copy them to the `/roms` folder on the MicroSD card. Games need a
MicroSD card. They save automatically when you leave them, and you can also use
save slots.

To change which gamepad button does what, tap **Controls** in the Inkade
library, tap an action, then press the button you want. Each action has one
button; picking a button already in use swaps the two. Select + Start together
always opens the pause menu.

On the Sticky, Inkade's fast video is a port of
[InkBoy](https://github.com/kuslota/InkBoy) by kuslota, with its settings and
presets; see [Inkade on the Sticky](docs/inkade-sticky.md). E-paper games can
leave faint ghost images; tap the game screen to clean them up.

## Building a Book32

Book32 is a Seeed Studio XIAO ESP32-S3 (16 MB flash, 8 MB PSRAM) connected to
a 7.5-inch 800 × 480 e-paper panel (GD7965 controller).

| E-paper pin | Function | XIAO ESP32-S3 pin |
| --- | --- | --- |
| VCC | 3.3 V | 3V3 |
| GND | Ground | GND |
| DIN | MOSI | GPIO 9 |
| CLK | SCK | GPIO 7 |
| CS | Chip select | GPIO 44 |
| DC | Data/command | GPIO 10 |
| RST | Reset | GPIO 38 |
| BUSY | Busy | GPIO 4 |

- **Button:** GPIO 5 to GND.
- **Battery sense:** voltage on GPIO 1; the measurement switch on GPIO 6
  (active high).

## Releases

Every release, with notes and firmware files, is on the
[releases page](https://github.com/rolohaun/Book32/releases). Devices update
from the latest release when you start an update from the web interface or the
device menu.

## License

InkDeck is licensed under the GNU Affero General Public License, version 3 or
later; see [LICENSE.md](LICENSE.md) and [COPYING](COPYING). It includes
third-party code under its own licenses, listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Each release includes the
complete source code and license notices.
