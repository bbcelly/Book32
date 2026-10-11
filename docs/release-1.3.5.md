# InkDeck 1.3.5

Released October 11, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## Klipper: finds printers reliably (all devices)

The Klipper app could scan the whole network and miss a printer that was online.
Each address got only 0.5 s to answer. A Wi-Fi printer in power-save mode can
take longer than that to reply the first time, and if it missed the single
address lookup broadcast, the device didn't ask again for about 3 seconds.

- **Wi-Fi power saving is off while the app is open.** It's restored when you
  leave the app.
- **Faster, more forgiving scan.** Eight addresses are probed at once with a
  1.5 s timeout, and the address lookup is re-sent every 500 ms. A full scan now
  takes about 46 s instead of about 2.5 minutes.
- **Checks Moonraker on the connection it already has.** The scan no longer
  reconnects to confirm, which could miss a printer the scan had just found.
- **Found printers are remembered.** Reopening the app reconnects in under a
  second without scanning again.
- **Waits for Wi-Fi.** If Wi-Fi is still reconnecting, for example after the
  eReader turned it off, the app shows "Waiting for Wi-Fi…" and starts the scan
  once connected. It used to report "No printers found".
- **Printer names from Fluidd.** Names set in Fluidd are now read, as well as
  names set in Mainsail.

## eReader: sharper covers, faster library

- **Sharper covers.** Covers were shrunk by keeping only some of the pixels and
  dropping the rest, which broke up titles and erased thin lines. They are now
  scaled by averaging every pixel, contrast-stretched and lightly sharpened
  before dithering. This applies to all devices and to images inside books.
- **Larger covers on Book32.** Covers are 90×120 instead of 60×80, and the
  library shows 4 books per screen so titles on the covers are readable.
- **Covers decoded once per book.** Each decoded cover is saved in a hidden
  `/.covers` folder, so it is only decoded again when the book is new or
  replaced. Saved covers are removed when their book is deleted.
- **No loading screen unless needed.** Opening the eReader goes straight to the
  library unless a book needs its title or cover read.

## All three devices

Firmware and web-interface images are rebuilt as 1.3.5 for each board. Pinouts
and partition layouts are unchanged. The flasher updates both the application
and its web interface; the installer does not format the SD card.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect. The
release includes complete tagged source, the dependency source archive and
license notices.

## Known issue

Some EPUBs use progressive JPEG covers. The JPEG decoder library InkDeck uses
can overrun its buffers on large progressive JPEGs. This predates this release
and is being investigated separately.

## Verification

All three firmware builds, the release-package and installer checks, and the
repository's host tests pass. On Book32 hardware, the Klipper app found a
Fluidd/Moonraker printer by full scan (about 46 s) and from the remembered
address (0.43 s), then showed its live status. The new cover pipeline was
compared against the old one by compiling the firmware's image code on a PC and
rendering real covers, then checked on the device.
