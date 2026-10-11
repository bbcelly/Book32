# InkDeck 1.3.3

Released October 11, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## Book32: faster startup

- The boot screen stays, but its progress updates are now drawn while startup
  continues instead of pausing for every 7.5" panel refresh. The screen always
  shows the newest real step and skips steps that already finished, so the
  device no longer waits on progress it has already passed. Boot to the main
  menu drops from about 15 seconds to about 9.
- The display orientation is remembered in device settings, so the first boot
  frame is drawn the right way up. A rotated Book32 no longer pays for an extra
  full refresh at boot.
- The display link runs at 10 MHz instead of 4 MHz.
- Startup no longer walks the internal ebook storage just to log its size.
- When the saved Wi-Fi network is out of range, the setup portal now opens
  after 15 seconds instead of up to 60.

## Book32: dark artifacts on the main menu

The right-hand edge of the screen slowly darkened while the main menu was open.
Three causes are fixed:

- The panel's drive voltage stayed on after every partial update. It is now
  switched off as soon as each update finishes.
- Noise in the battery voltage reading toggled the "charging" state every few
  seconds, and each toggle repainted the panel. Charging is now detected only
  from the 90-second voltage trend.
- The menu battery icon is repainted only for a visible change (5% or a
  charging change), and the menu performs a cleanup full refresh after every
  20 partial updates, as the reader already does.

Existing darkening clears on the first full refresh after updating.

## All three devices

Firmware and web-interface images are rebuilt as 1.3.3 for each board. Sticky
and LILYGO behavior is unchanged apart from the version number and the
remembered orientation. Pinouts and partition layouts are unchanged. The
flasher updates both the application and its web interface; the installer does
not format the SD card.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect.
The release includes complete tagged source, the dependency source archive
and license notices.

## Verification

All three firmware builds, the release-package and installer checks, and the
repository's host tests pass. The Book32 build was flashed to hardware: serial
logs confirm the shorter boot sequence, the panel powering off between updates
and no charging-state toggling while idle.
