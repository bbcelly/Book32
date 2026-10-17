"""Driver for the Waveshare 7.5 inch V2 e-Paper HAT (800 x 480, black and white).

Screens are drawn at the logical (portrait) size and rotated here. Page turns
use the panel's partial refresh; a full refresh every few page turns clears
ghosting. Without the Waveshare library (on a computer), each frame is saved to
data/display.png instead.
"""

import logging
import sys

from PIL import Image

from .paths import APP_ROOT, data_path

# install.sh copies Waveshare's Python library here.
sys.path.insert(0, str(APP_ROOT / "lib"))

HW_WIDTH, HW_HEIGHT = 800, 480


class DisplayDriver:
    def __init__(self, width: int = 480, height: int = 800, rotation: int = 90):
        self.logger = logging.getLogger(__name__)
        self.width = width
        self.height = height
        self.rotation = rotation
        self.epd = None
        self.full_refresh_interval = 10
        self.partial_count = 0
        self.partial_mode = False
        self.first_frame = True
        try:
            from waveshare_epd import epd7in5_V2
            self._module = epd7in5_V2
            self.hardware_available = True
        except ImportError:
            self._module = None
            self.hardware_available = False
            self.logger.warning("Waveshare library not found: saving frames to data/display.png")
        except Exception as e:  # The library claims GPIO pins when imported
            self._module = None
            self.hardware_available = False
            self.logger.error(f"E-paper display unavailable ({e}): saving frames to data/display.png")

    def initialize(self):
        if self.hardware_available:
            self.epd = self._module.EPD()
            self.epd.init()
            self.logger.info("E-paper display initialized")

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

        full = (self.first_frame or not use_partial
                or (count_page_turn and self.partial_count >= self.full_refresh_interval))
        buffer = self.epd.getbuffer(image)
        if full:
            if self.partial_mode:
                self.epd.init()
                self.partial_mode = False
            self.epd.display(buffer)
            self.partial_count = 0
            self.first_frame = False
            self.logger.debug("Full refresh")
            return
        if not self.partial_mode:
            self.epd.init_part()
            self.partial_mode = True
        self.epd.display_Partial(buffer, 0, 0, HW_WIDTH, HW_HEIGHT)
        if count_page_turn:
            self.partial_count += 1
        self.logger.debug(f"Partial refresh ({self.partial_count}/{self.full_refresh_interval})")

    def reinitialize(self):
        """Return the panel to full-refresh mode (before a final frame)."""
        if self.epd:
            self.epd.init()
            self.partial_mode = False

    def cleanup(self):
        """Put the panel into deep sleep; it keeps showing the last frame."""
        if self.epd:
            try:
                self.epd.sleep()
            except Exception as e:
                self.logger.error(f"Display sleep failed: {e}")
