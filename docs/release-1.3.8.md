# InkDeck 1.3.8

Released October 12, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## Your book's cover while the device sleeps

When a device goes to sleep it now shows the cover of the book you were last
reading, filling the screen, with your sleep message underneath. This is the new
default on all three devices.

- **Made for a full screen.** Covers are averaged down from the original image,
  levelled, lightly sharpened and dithered with serpentine Floyd-Steinberg,
  which keeps smooth skies, skin and fog instead of the hard dots used for small
  library tiles.
- **Coloured text survives.** Plain grayscale turns some colours, like bright
  red lettering on a dark background, into almost the same gray as their
  surroundings. Covers now add brightness for vivid colours, the way the eye
  sees them, so that lettering stays readable.
- **Choose a message instead.** In the web interface's Settings page, set
  *While asleep, show* to *Sleep message only*. On the Sticky and LILYGO, open
  Settings > Sleep screen on the device.
- The message-only screen is used automatically when no book has been opened
  yet, the book has no cover, or the cover is a progressive JPEG (these are
  skipped because the JPEG decoder can overrun on them). A low-battery shutdown
  always shows its charging warning.

## Book32: the same sleep as the Sticky and LILYGO

- **Sleep is on by default after 10 minutes** of inactivity. If you never
  changed the timeout (it was previously off by default), it becomes 10 minutes
  once; set it back to 0 in the web interface to turn automatic sleep off.
- Before sleeping, Book32 now turns Wi-Fi fully off (including the setup
  hotspot), puts the e-paper controller into its own deep sleep, and holds the
  battery-sensing switch and display control lines at fixed levels so they
  cannot draw current or glitch the panel.
- Wake-up waits for the button to be released, so the press that wakes the
  device is not also taken as a page turn.

## Settings

- The web interface's sleep card gains *While asleep, show* (book cover or
  sleep message). Saving one sleep setting no longer resets the others.
- On the Sticky and LILYGO, the Settings home row *Sleep message* becomes
  *Sleep screen*, which opens a page with the cover/message choice and the
  message editor.

## All three devices

Firmware and web-interface images are rebuilt as 1.3.8 for each board. Pinouts
and partition layouts are unchanged. The flasher updates both the application
and its web interface; the installer does not format the SD card.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect. The
release includes complete tagged source, the dependency source archive and
license notices.

## Verification

All three firmware builds, the release-package and installer checks, and the
repository's host tests pass. On Book32 hardware, sleep was entered over serial
and checked with photographs: the cover rendered full screen with the message
underneath, the device reached deep sleep, and a button press woke it twice.
The settings API was checked switching between cover and message. The Sticky
and LILYGO builds share the same sleep-screen code but were not tested on their
hardware for this release.
