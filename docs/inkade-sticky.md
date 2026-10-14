# Inkade on the Seeed Studio Sticky

Inkade plays Game Boy and Game Boy Color games on the Sticky's 800x480 SSD1677
panel, shown at 2x (320x288). Its fast display drive is a straight port of
[InkBoy](https://github.com/kuslota/InkBoy) by kuslota (MIT): the same
algorithms, parameters and defaults, adapted only where the Sticky's hardware
differs. See InkBoy's
[how it works](https://github.com/kuslota/InkBoy/blob/main/docs/how-it-works.md)
and [tuning guide](https://github.com/kuslota/InkBoy/blob/main/docs/tuning.md)
for the details; the code is in `lib/Book32_Core/InkBoyDrive.h` and the game
section of `lib/Book32_Core/StickyDisplay.cpp`.

> **Experimental.** Custom waveforms and fast drive may cause image retention.
> Long-term effects on the panel have not been measured.

## What is the same as InkBoy

- **Per-pixel drive.** A short one-frame waveform; each scan pulses only the
  pixels that still need drive (black or white) and holds the rest. Drive is
  counted as consumed only after a scan is delivered. When the last pixel
  finishes, one all-HOLD scan ends the waveform cleanly.
- **Fast grayscale (default).** Pixels stop partway between black and white.
  Each of the 12 changes of shade has its own dose count (InkBoy's table), with
  the per-pixel level model, retargeting at 100%, pair hold mode (a drive scan
  plus an immediate follow-up scan, with hold flags `0xc667`), and history
  corrections (L>K>L: -1 on a completed return, 150% on a reversal).
- **Fast dithered.** A fixed 4x4 Bayer pattern (grays about 5/16 and 11/16
  black); each changed pixel gets 8 pulses toward black or white.
- **Pacing.** The emulator runs on its own 59.7 fps clock; the display samples
  the newest finished frame on a fixed 33.3 ms image clock, skipping missed
  slots. Scans start at least 30 ms (dithered) or 15 ms (grayscale) apart; in
  pair mode a pair counts as one 30 ms slot. The analog supply is switched off
  after 750 ms without changes.
- **Settings and presets.** Mode, dithered black/white pulses, the dose table,
  hold flags, retarget dose, scans per frame, pair hold mode, history and the
  six round-trip routes; Show FPS; and the transition test (four bands with
  moving rectangles). Presets use InkBoy's `inkboy-preset-v1` JSON in
  `/inkboy/presets/dithered/` and `/inkboy/presets/grayscale/`, so InkBoy
  presets can be copied straight onto the card.

## What differs, and why

| InkBoy (SSD1681, 200x200) | Sticky (SSD1677, 800x480) |
| --- | --- |
| Game at 1:1 on a 200x200 panel | Game at 2x: each Game Boy pixel is a 2x2 cell with one shared state (grayscale), or four Bayer dots (dithered) |
| Waveform frames of ~20 ms (dithered) / ~14 ms (grayscale) | 4 and 3 frames of 5 ms: 20 ms and 15 ms |
| 5,000-byte planes at 20 MHz | 11,520-byte planes at 20 MHz |
| LIGHTEN voltage and SETTLED W>L options | Not available: they set SSD1681 source voltages |

Because each scan sends 4.6x as many bytes, the Sticky reaches about 25
updates per second in dithered mode and about 17 in grayscale pair mode
(InkBoy: about 30). The emulator still runs at a full 60 fps.

## Settings

Open **Inkade > Display >**. Changes apply the next time a game starts or the
transition test opens, and are saved in `/inkade/settings.json`.

## USB serial commands (development)

At 115200 baud the Sticky accepts, one per line: `tap X Y`, `hold X Y MS`,
`sleep`, and in the Inkade app with no game open: `set mode gray|dither`,
`set show_fps 0|1`, `set black N`, `set white N`, `set dose KL N` (from, to as
K/D/L/W), `set hold KL 0|1`, `set retarget 50..150`, `set pair 0|1`,
`set policy auto|one`, `set history 0..3`, `set return LKL N`,
`set reverse LKL N` (0 = base), `show`.

## Safety

The driver sends only a reviewed list of volatile SSD1677 commands. Each is
checked against that list at compile time, and
`test/sticky_command_allowlist.cjs` fails if the list changes or a
one-time-programmable command appears. These checks do not establish
long-term panel safety.

## Grayscale sleep covers

Separate from Inkade: the Sticky's sleep screen draws the book cover in gray by
pulsing it in from white in seven measured tones with error diffusion.
