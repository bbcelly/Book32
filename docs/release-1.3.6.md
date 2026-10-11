# InkDeck 1.3.6

Released October 11, 2026 for Book32, Seeed Studio Sticky and LILYGO T5 E-Paper
S3 Pro H752-01 / Pro Lite. Install with the matching device card in the
[browser flasher](https://rolohaun.github.io/Book32/).

## Fixed: reboot after leaving the Klipper app (all devices)

Leaving the Klipper app could restart the device about five seconds later.

When the app opened, it took the web server's network task off the system
watchdog, and when the app closed it put the task back. But the web server
(AsyncTCP) only joins the watchdog while it is handling a request and leaves
again afterwards. Putting it back permanently meant it sat idle on the
watchdog, never checked in, and the watchdog restarted the device.

The Klipper app no longer changes the watchdog at all. Printer discovery,
remembered printers and status updates from 1.3.5 are unchanged.

## All three devices

Firmware and web-interface images are rebuilt as 1.3.6 for each board. Pinouts
and partition layouts are unchanged. The flasher updates both the application
and its web interface; the installer does not format the SD card.

InkDeck remains AGPLv3-or-later; third-party licenses remain in effect. The
release includes complete tagged source, the dependency source archive and
license notices.

## Verification

All three firmware builds, the release-package and installer checks, and the
repository's host tests pass. The reboot was identified in a Book32 serial log
(`task_wdt: async_tcp` abort five seconds after `Klipper App Stopped`).
