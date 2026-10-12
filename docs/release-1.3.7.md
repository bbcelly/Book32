# InkDeck 1.3.7

Released October 12, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## Ink Boy on the Seeed Studio Sticky

Ink Boy, InkDeck's Game Boy and Game Boy Color player, now runs on the Sticky.
Add your own `.gb` / `.gbc` ROMs to `/roms` on the MicroSD card, or upload them
from the web interface's Ink Boy tab, then open Ink Boy from the menu.

- **Fast e-paper video.** The Sticky's SSD1677 controller is driven directly
  with a short custom waveform and per-pixel drive budgets: each pixel that
  changes gets two 25 ms pulses, and settled pixels are not driven at all.
  Display-only updates skip the controller's ~82 ms power-up, and the pixel
  bookkeeping handles 32 pixels per step. Measured on hardware: the emulator
  runs at a full 60 fps and the screen updates 30-58 times a second (about 30-42
  while a whole map scrolls).
- **Four grey levels.** The game is shown at 2x (320x288); each Game Boy pixel
  is a 2x2 cell with 0, 1, 3 or 4 black dots.
- **Ghosting.** A still picture is cleaned automatically after about a second,
  with no flash. Tap the game screen to clean it at any time. Fast scrolling
  still softens the picture until it settles.
- **Controls.** Multi-touch D-pad, A, B, Select and Start below the game; the
  side Up/Down keys also act as A and B; the main button opens Pause / saves.
  Save slots and automatic resume work as on the LILYGO.
- **Game Boy only for now.** NES (256x240) does not fit the Sticky's 2x window
  yet, so NES ROMs are not listed on the Sticky.
- The main menu gains an Ink Boy tile; its five tiles use tighter rows to fit
  the 800 px screen.

## Fixed: colour games lost their background after resuming (Sticky, LILYGO)

Save states store a Game Boy Color game's raw palettes but not the grey
lookup tables built from them, and nothing rebuilt those tables after a load.
A resumed colour game therefore drew its background white (sprites still
showed) until the game next changed palettes. All grey tables are now rebuilt
whenever a save loads.

## Other changes

- **New Settings icon** on the Sticky and LILYGO menus: an outlined gear that
  matches the other app icons.
- The web interface's games page is labelled Ink Boy for every device that
  supports it.

## All three devices

Firmware and web-interface images are rebuilt as 1.3.7 for each board. Pinouts
and partition layouts are unchanged. The flasher updates both the application
and its web interface; the installer does not format the SD card.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect. The
release includes complete tagged source, the dependency source archive and
license notices.

## Verification

All three firmware builds, the release-package and installer checks, and the
repository's host tests pass. On Sticky hardware, the video driver was measured
over serial, and gameplay was tested remotely and checked with photographs:
picture orientation, four-shade rendering, ghosting at several pulse settings,
and a Game Boy Color game resumed from a save.
