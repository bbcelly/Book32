# InkDeck 1.4.3

Released October 16, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## New: remap gamepad controls in Inkade

Inkade's library has a **Controls** button (on the Sticky and the LILYGO). It
lists each game button (A, B, Select, Start, the four directions) and the pause
menu with the gamepad button that drives it. Tap an action, or choose it with
the d-pad and A, then press the gamepad button you want. Each action has
exactly one button; choosing a button another action already uses swaps the
two. **Reset to defaults** restores the standard layout.

- The L2 and R2 triggers can now be assigned.
- Select + Start together always opens the pause menu, whatever the mapping.
- Mappings are kept in the device's flash: they need no SD card and survive
  updates. Each device keeps its own.
- The default layout puts each action on the button of the same name, with
  Home for the pause menu. X and Y no longer also act as A and B; assign them
  in Controls if you prefer them.

## Fixed: LILYGO sleep cover

- The cover was cut off: only its top 524 rows were drawn, so most covers lost
  their bottom third.
- Covers looked washed out. FastEPD's stock grayscale waveform, made for
  another panel, put this display's 16 levels out of order and had nothing
  between black and a light gray. The LILYGO now uses its own waveform,
  measured on the panel, with levels that run in order from black to white,
  and maps covers to the tones it actually shows.
- Covers are no longer sharpened on the LILYGO, which only added grain to a
  true-gray picture.

## All three devices

Firmware and web-interface images are rebuilt as 1.4.3 for each board. Pinouts
and partition layouts are unchanged. The flasher updates both the application
and its web interface; the installer does not format the SD card.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect. The
release includes complete tagged source, the dependency source archive and
license notices.

## Verification

All three firmware builds and the repository's tests pass, including a new
gamepad-mapping test and Controls hit-geometry checks. On the LILYGO, the sleep cover was photographed after
a real sleep, and the Controls screen was opened and a button captured from an
8BitDo Ultimate 2C Wireless before the one-to-one change; the one-to-one screen
was photographed but not yet exercised with a pad. On the Sticky, the new
screens were checked in a host rendering of the drawing code, not on the
device.
