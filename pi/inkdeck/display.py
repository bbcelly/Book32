"""Display for the Waveshare 7.5 inch V2 e-Paper HAT (800 x 480, black and white).

Screens are drawn at the logical (portrait) size and rotated here. Page turns
use the panel's partial refresh; a full refresh every few page turns clears
ghosting. The panel driver is in hardware/epd7in5.py. Without the panel (on a
computer), each frame is saved to data/display.png instead.
"""

import logging
import threading
import time

from PIL import Image

from .paths import data_path

HW_WIDTH, HW_HEIGHT = 800, 480
IDLE_POWER_OFF = 10  # Seconds; quick page turns skip the panel's power-up


class DisplayDriver:
    def __init__(self, width: int = 480, height: int = 800, rotation: int = 90, spi_hz: int = 10_000_000):
        self.logger = logging.getLogger(__name__)
        self.width = width
        self.height = height
        self.rotation = rotation
        self.spi_hz = spi_hz
        self.epd = None
        self.full_refresh_interval = 10
        self.partial_count = 0
        self.last_frame = 0.0
        self._lock = threading.Lock()  # The monitor thread powers the panel off
        try:
            import gpiozero  # noqa: F401
            import spidev  # noqa: F401
            self.hardware_available = True
        except ImportError:
            self.hardware_available = False
            self.logger.warning("spidev or gpiozero missing: saving frames to data/display.png")

    def initialize(self):
        if not self.hardware_available:
            return
        from .hardware.epd7in5 import EPD7in5
        try:
            self.epd = EPD7in5(self.spi_hz)
            self.logger.info("E-paper display ready")
        except Exception as e:  # No SPI, or the GPIO pins are taken
            self.epd = None
            self.logger.error(f"E-paper display unavailable ({e}): saving frames to data/display.png")

    def set_full_refresh_interval(self, interval: int):
        self.full_refresh_interval = max(1, int(interval))

    def reset_partial_counter(self):
        self.partial_count = 0

    def display_image(self, image: Image.Image, use_partial: bool = True, count_page_turn: bool = False):
        """Show a frame. A full refresh happens on the first frame, when
        use_partial is False, and after every `full_refresh_interval` counted
        page turns."""
        if image.size != (self.width, self.height):
            self.logger.warning(f"Frame is {image.size}, expected {(self.width, self.height)}; resizing")
            image = image.resize((self.width, self.height), Image.Resampling.NEAREST)
        if image.mode != "1":
            image = image.convert("1", dither=Image.Dither.NONE)
        if self.rotation:
            image = image.rotate(-self.rotation, expand=True)

        if not self.epd:
            image.save(data_path("display.png"))
            return

        full = not use_partial or (count_page_turn and self.partial_count >= self.full_refresh_interval)
        started = time.monotonic()
        with self._lock:
            try:
                self.epd.display(image.tobytes(), full)
            except Exception as e:
                self.logger.error(f"Display update failed: {e}")
                return
            finally:
                self.last_frame = time.monotonic()
        if full:
            self.partial_count = 0
        elif count_page_turn:
            self.partial_count += 1
        self.logger.debug(f"{'Full' if full else 'Partial'} refresh in {self.last_frame - started:.2f}s "
                          f"({self.partial_count}/{self.full_refresh_interval})")

    def idle(self):
        """Called about once a second: power the panel down once frames stop."""
        if self.epd and self.epd.powered and time.monotonic() - self.last_frame > IDLE_POWER_OFF:
            with self._lock:
                try:
                    self.epd.power_off()
                except Exception as e:
                    self.logger.error(f"Display power-off failed: {e}")

    def cleanup(self):
        """Put the panel into deep sleep; it keeps showing the last frame."""
        if self.epd:
            with self._lock:
                try:
                    self.epd.close()
                except Exception as e:
                    self.logger.error(f"Display sleep failed: {e}")
            self.epd = None
