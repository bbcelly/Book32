# InkDeck 1.4.2

Released October 16, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## Fixed: gamepads that pair themselves stuck on "connecting"

Some Bluetooth LE gamepads, such as the 8BitDo Ultimate 2C Wireless, start
pairing on their own as soon as they connect and never finish it. InkDeck
waited for that pairing with no time limit, so the gamepad stayed on
"connecting" and never reached menus or Inkade. InkDeck now waits at most ten
seconds and then continues, which these pads accept.

The fix is in the gamepad support shared by all three devices: Inkade on the
Sticky and LILYGO, and menus and the eReader everywhere.

## All three devices

Firmware and web-interface images are rebuilt as 1.4.2 for each board. Pinouts
and partition layouts are unchanged. The flasher updates both the application
and its web interface; the installer does not format the SD card.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect. The
release includes complete tagged source, the dependency source archive and
license notices.

## Verification

All three firmware builds pass, as do the repository's tests except the InkBoy
touch-layout host test, which was not built for this release (its code is
unchanged). On the Sticky, an
8BitDo Ultimate 2C Wireless connected within seconds of boot and played a Game
Boy game in Inkade at 60 fps; its d-pad, A, X and Start reached the game. The
LILYGO uses the same gamepad code but was not tested with a pad for this
release.
