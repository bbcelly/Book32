# InkDeck 1.4.0

Released October 14, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## Inkade on Book32

Book32 now runs Inkade, InkDeck's Game Boy / Game Boy Color player. Upload
`.gb` and `.gbc` games from the web interface; they are stored in Book32's
internal storage. Book32 has no touch screen, so games are played with a
Bluetooth LE gamepad (pair it under Settings). The library can be browsed with
the gamepad or the side button.

The game window uses the same fast per-pixel video as the Sticky, driven on
Book32's 7.5" panel at a raised frame rate. It starts in dithered mode. This is
experimental: expect some streaking and ghosting while tuning continues.

## Bluetooth gamepads on every device

All three devices can pair a Bluetooth LE gamepad (Settings, or the web
interface's Settings card). In Inkade the D-pad and A / B / Select / Start act
as on a Game Boy, X and Y also work as A and B, and Home (or Select + Start)
pauses. Outside games, the gamepad moves through menus, and Home returns to
the main menu.

Only Bluetooth LE gamepads work, such as Xbox Series X|S controllers with
current firmware. The ESP32-S3 has no Classic Bluetooth, so controllers that
pair only over Classic Bluetooth cannot connect.

## Grayscale sleep covers on every device

The book cover shown while a device sleeps is now drawn in true gray instead
of a black-and-white dot pattern: on the Sticky and Book32 with short drive
pulses, on the LILYGO in its 16-level gray mode.

## Inkade on the Sticky follows InkBoy

"Ink Boy" is now called **Inkade**. The Sticky's fast game video is a port of
[InkBoy](https://github.com/kuslota/InkBoy) by kuslota (MIT), with its
algorithms, defaults and settings format; InkBoy presets load unchanged. See
[Inkade on the Sticky](inkade-sticky.md).

## Fixed

- Book32 no longer goes to sleep during a game upload, and no longer goes to
  sleep about 20 seconds after starting up.

## All three devices

Firmware and web-interface images are rebuilt as 1.4.0 for each board. Pinouts
and partition layouts are unchanged. The flasher updates both the application
and its web interface; the installer does not format the SD card.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect,
including InkBoy (MIT) and NimBLE-Arduino (Apache-2.0). The release includes
complete tagged source, the dependency source archive and license notices.

## Verification

Book32 and the Sticky were tested on hardware: Inkade on Book32 (library and
Super Mario Land, driven over the serial console), Inkade on the Sticky, and
the grayscale sleep cover on both. Gamepad pairing was exercised with a
controller that offers no standard gamepad service over Bluetooth LE, so no
gamepad has yet played a game on hardware; the HID report decoder passes its
host tests. The LILYGO's grayscale cover was built but not yet run on
hardware. Host tests for the InkBoy drive pass.
