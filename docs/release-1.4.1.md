# InkDeck 1.4.1

Released October 14, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## Inkade removed from Book32

Inkade (Game Boy / Game Boy Color) is no longer on Book32. Its 7.5" panel needs
far more drive per change than the Sticky's, so games showed heavy ghosting
and streaking, and the game window's refreshes grayed the rest of the screen.
Book32 returns to its pre-1.4.0 main menu. Games uploaded to Book32 under 1.4.0
remain in its internal storage, unused.

Book32 keeps the rest of 1.4.0: the grayscale sleep cover and Bluetooth LE
gamepad support for menus.

## Fixed: Inkade icon corners

The Inkade icon's rounded frame on the Sticky and LILYGO main menus had gaps
in its corners. It is now drawn with an even border.

## All three devices

Firmware and web-interface images are rebuilt as 1.4.1 for each board. Pinouts
and partition layouts are unchanged. The flasher updates both the application
and its web interface; the installer does not format the SD card.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect. The
release includes complete tagged source, the dependency source archive and
license notices.

## Verification

All three firmware builds and the repository's tests pass. The new icon frame
was checked in a rendering of the graphics library's drawing routines, not yet
photographed on a device. The LILYGO grayscale cover from 1.4.0 has still not
been run on hardware.
