# InkDeck 1.3.4

Released October 11, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## Book32: faster page turns

Measured on Book32 hardware:

| | 1.3.3 | 1.3.4 |
|---|---|---|
| Page turn | ~1.8 s | ~1.15 s |
| Page turn with a full refresh | ~4.1 s | ~1.8 s |
| Full refresh (reader, menu, boot) | 3.9 s | 1.6 s |

- **Shorter page-turn waveform.** Partial updates use a shorter, still
  charge-balanced waveform (0.98 s instead of 1.6 s).
- **Faster full refresh.** Full refreshes use the panel's shorter
  high-temperature waveform, as GxEPD2 does for the GDEY075T7 panel. They still
  flash and clear ghosting. Boot and opening the main menu speed up as well.
- **Progress saving moved off the page turn.** Reading progress is now saved
  two seconds after you stop turning pages, instead of before each page is
  drawn. That write took up to 0.8 s on internal flash. Leaving a book,
  switching apps and sleeping still save immediately.
- **Panel stays powered briefly after a button press.** The panel is kept on
  for 10 seconds after a press, so quick page turns skip the ~130 ms power-up.
  It still powers down afterwards, which keeps the 1.3.3 fix for edge
  artifacts in place.
- **Per-turn timing in the serial log.** Each page turn logs its timing, for
  diagnosing future slowdowns.

The button itself was not the cause: a press is acted on about 30 ms after
release.

## All three devices

Firmware and web-interface images are rebuilt as 1.3.4 for each board. Sticky
and LILYGO also save reading progress after a pause instead of on every page
turn; their displays are otherwise unchanged. Pinouts and partition layouts are
unchanged. The flasher updates both the application and its web interface;
the installer does not format the SD card.

The Book32 display driver is a modified copy of GxEPD2's `GxEPD2_750_T7`
(GPL-3.0); see THIRD_PARTY_NOTICES.md. InkDeck remains AGPLv3-or-later. The
release includes complete tagged source, the dependency source archive and
license notices.

## Verification

All three firmware builds, the release-package and installer checks, and the
repository's host tests pass. On Book32 hardware, serial logs confirm the page
turn, full refresh and boot timings above. Text contrast and ghosting were
checked by eye.
