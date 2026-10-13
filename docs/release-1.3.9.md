# InkDeck 1.3.9

Released October 12, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## Fixed: the Sticky said "Press BOOT to wake"

Each device shows its own wake instruction while asleep: the Sticky and Book32
say "Press power to wake", the LILYGO says "Press BOOT to wake". Sleep settings
are stored on the SD card, so a card that had been in a LILYGO (or a setting
saved from one) carried the LILYGO's message to the Sticky, which has no BOOT
button.

The Sticky and Book32 now replace "Press BOOT to wake" with their own message
wherever it is shown: on the sleep screen, in the device's Settings and in the
web interface. The corrected setting is saved back. A message you wrote
yourself is never changed.

## Also in this release

- The browser flasher's device cards no longer overflow when a browser holds
  an older copy of the page's stylesheet.

## All three devices

Firmware and web-interface images are rebuilt as 1.3.9 for each board. Pinouts
and partition layouts are unchanged. The flasher updates both the application
and its web interface; the installer does not format the SD card.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect. The
release includes complete tagged source, the dependency source archive and
license notices.

## Verification

All three firmware builds, the release-package and installer checks, and the
repository's host tests pass. The sleep-message rule's test was compiled for
each board's configuration. This fix was not run on device hardware.
