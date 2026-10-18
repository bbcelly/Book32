# InkDeck 1.4.4

Released October 17, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## New: PDFs and manga in the eReader (Sticky and LILYGO)

Upload a PDF, or a manga or comic as a CBZ or ZIP of page images, in the web
interface. Your browser turns it into pages sized for the device before
uploading, so any PDF works, scanned or not, and so do pages in WebP or
progressive JPEG. The PDF reader (PDF.js) is stored on the device, so this
works without internet; only PDFs whose Asian fonts aren't embedded fetch
extra character data. A CBZ copied straight onto the MicroSD card opens too
if its pages are plain JPEG or PNG.

- **Pinch to zoom** (up to 4x) and **drag** a zoomed page, with live frames
  while your fingers move: about 14 per second on the Sticky and 9 on the
  LILYGO, drawn on the second processor core with the fast drive Inkade uses.
  The page is redrawn in full quality when you let go.
- **Tap the middle** of a page to zoom in on that spot, and again to zoom out.
- **Guided view:** zoomed in, the next button (or a tap on the right side)
  moves one screen along the page in reading order, then down a row, then on
  to the next page.
- **Swipe** or tap the sides to turn pages. The next page is prepared in the
  background, so turning waits only for the display.
- **Manga page order** (right to left) in the reader settings.
- **LILYGO:** whole pages in 16 grays (**Grayscale pages**), and a choice for
  zoomed views: **Fast** (black and white, no flash; the default) or
  **Grayscale** (16 grays, a full refresh per step).
- These options are in the web interface's reader settings and on the device
  under **Settings > Reading display**. Saving that page on the device now
  keeps reader settings made in the web interface.
- Covers in the library and on the sleep screen come from the first page.

## New: library pages

When there are more books than fit on the screen, the library shows a page at
a time, with **Prev** and **Next** buttons and "Page X of Y" on the touch
devices. Every book can now be reached by touch on the LILYGO, which has no
next and previous buttons.

## Fixed: large uploads

- On the Sticky, the MicroSD card now and then rejects a write during a long
  upload. Writes are now gathered into 64 KB blocks and written again when the
  card rejects one, so large books arrive intact.
- An upload that fails or is interrupted is removed instead of leaving a
  cut-off book in the library, and the web page reports the error.
- Uploads count as activity, so idle sleep no longer cuts them off.
- The screen doesn't redraw during an upload (the Sticky's card shares the
  display's SPI bus).

## All three devices

Firmware and web-interface images are rebuilt as 1.4.4 for each board. Pinouts
and partition layouts are unchanged. Book32 builds without the PDF and manga
features (they need a touch screen and a MicroSD card). Updating replaces the
web-interface partition, which also holds the original names of uploaded
files: books keep working, and titles fall back to the book's own title or
its stored file name.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect. PDF.js
(Apache 2.0) is added to the third-party notices. The release includes
complete tagged source, the dependency source archive and license notices.

## Verification

All three firmware builds and the repository's tests pass, including a new
test of the browser-side ZIP writer. On the Sticky, a 157-page manga (424 MB
CBZ, 46 MB converted) and a 305-page PDF (99 MB, 42 MB converted) were
uploaded, opened, turned and zoomed, with simulated pinch and drag gestures
over serial and photographs of the screen. On the LILYGO, live pinch and drag,
guided zoom steps without a full refresh, the zoomed-view setting and library
paging were photographed. Gestures were last tried by hand on an earlier
build; this build's pinch and drag were tested with simulated input.
