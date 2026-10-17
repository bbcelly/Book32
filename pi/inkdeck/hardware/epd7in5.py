"""Fast driver for the Waveshare 7.5 inch V2 e-Paper HAT (800 x 480, UC8179
controller), in place of Waveshare's Python library.

The panel waveforms are the ones InkDeck uses on the ESP32 Book32, which has
the same panel (GxEPD2_750_T7_Fast):

- Full refresh forces the controller's temperature reading to 90 C, so it
  picks the shorter high-temperature waveform from its OTP memory: about
  1.6 s instead of 3.9 s, and it still clears ghosting.
- Partial refresh (page turns) forces 110 C, as Waveshare's own init_part
  does: about 0.6 s.

Speed-ups over Waveshare's library: frames go to the panel as Pillow's own
bytes (the data polarity is set so 1 is white, as in Pillow), with no Python
loop over pixels; the old-frame buffer is kept by the controller (N2OCP), so
each refresh sends one frame instead of two; SPI runs at 10 MHz; switching
between full and partial refresh rewrites two registers instead of resetting
and powering the panel up again; and waiting watches the BUSY pin instead of
polling the controller over SPI.
"""

import time

WIDTH, HEIGHT = 800, 480
FRAME_BYTES = WIDTH * HEIGHT // 8

# Pins of the Waveshare HAT (BCM numbering).
RST_PIN, DC_PIN, BUSY_PIN, PWR_PIN = 17, 25, 24, 18

# Forced temperatures (command 0xE5) that select the OTP waveform.
FULL_TEMPERATURE = 0x5A     # 90 C: fast full refresh
PARTIAL_TEMPERATURE = 0x6E  # 110 C: fast partial refresh


class EPD7in5:
    def __init__(self, spi_hz: int = 10_000_000):
        import gpiozero
        import spidev

        self._rst = gpiozero.DigitalOutputDevice(RST_PIN, initial_value=True)
        self._dc = gpiozero.DigitalOutputDevice(DC_PIN)
        self._pwr = gpiozero.DigitalOutputDevice(PWR_PIN, initial_value=True)  # Newer HATs switch the panel power
        self._busy = gpiozero.DigitalInputDevice(BUSY_PIN, pull_up=False)
        self._spi = spidev.SpiDev()
        self._spi.open(0, 0)
        self._spi.max_speed_hz = spi_hz
        self._spi.mode = 0
        self.mode = None        # "full" or "partial": the registers currently loaded
        self.powered = False    # Panel drive voltages on
        self.sleeping = True    # Needs a reset before use
        self.first_frame = True

    # ---- Low level ----

    def _command(self, command: int, *data: int):
        self._dc.off()
        self._spi.writebytes([command])
        if data:
            self._dc.on()
            self._spi.writebytes(list(data))

    def _write_frame(self, command: int, frame: bytes):
        self._dc.off()
        self._spi.writebytes([command])
        self._dc.on()
        self._spi.writebytes2(frame)

    def _wait(self, timeout: float):
        """Wait while BUSY is low (the controller is working)."""
        time.sleep(0.005)
        deadline = time.monotonic() + timeout
        while not self._busy.value:
            if time.monotonic() > deadline:
                raise TimeoutError("e-paper panel stayed busy")
            time.sleep(0.005)

    def _reset(self):
        self._rst.on()
        time.sleep(0.02)
        self._rst.off()
        time.sleep(0.002)
        self._rst.on()
        time.sleep(0.02)
        self.sleeping = False
        self.powered = False
        self.mode = None

    def _init(self):
        """Power and resolution settings (Waveshare's values for this panel)."""
        self._reset()
        self.first_frame = True  # The controller's frame memory is cleared
        self._command(0x06, 0x17, 0x17, 0x28, 0x17)  # Booster soft start
        self._command(0x01, 0x07, 0x07, 0x28, 0x17)  # Power: VGH/VGL 20 V, VDH/VDL 15 V
        self._command(0x61, WIDTH >> 8, WIDTH & 0xFF, HEIGHT >> 8, HEIGHT & 0xFF)
        self._command(0x15, 0x00)                    # Dual SPI off
        self._command(0x60, 0x22)                    # TCON
        self._command(0xE0, 0x02)                    # Use the forced temperature (TSFIX)

    def _power_on(self):
        if not self.powered:
            self._command(0x04)
            self._wait(1)
            self.powered = True

    def _set_mode(self, mode: str):
        if self.sleeping:
            self._init()
        if mode != self.mode:
            self._command(0x00, 0x1F)  # Panel: black/white, waveform from OTP
            # VCOM and data interval: N2OCP (new data becomes old after each
            # refresh), data 1 = white; partial refresh leaves the border alone.
            self._command(0x50, 0x29 if mode == "full" else 0xA9, 0x07)
            self._command(0xE5, FULL_TEMPERATURE if mode == "full" else PARTIAL_TEMPERATURE)
            self.mode = mode
        self._power_on()

    # ---- Public ----

    def display(self, frame: bytes, full: bool):
        """Show a frame: 800 x 480 rows of packed bits, 1 = white (Pillow's
        mode "1" tobytes()). Returns when the panel has finished."""
        if len(frame) != FRAME_BYTES:
            raise ValueError(f"frame must be {FRAME_BYTES} bytes")
        full = full or self.first_frame
        self._set_mode("full" if full else "partial")
        if self.first_frame:
            self._write_frame(0x10, b"\xff" * FRAME_BYTES)  # Old frame: white
            self.first_frame = False
        if not full:
            self._command(0x91)  # Partial window: the whole screen
            self._command(0x90, 0, 0, (WIDTH - 1) >> 8, (WIDTH - 1) & 0xFF,
                          0, 0, (HEIGHT - 1) >> 8, (HEIGHT - 1) & 0xFF, 0x01)
        self._write_frame(0x13, frame)
        self._command(0x12)  # Refresh
        self._wait(10)
        if not full:
            self._command(0x92)  # Partial window off

    def power_off(self):
        """Turn the drive voltages off (the picture stays). The next frame
        powers up again, which takes about 0.1 s."""
        if self.powered and not self.sleeping:
            self._command(0x02)
            self._wait(1)
            self.powered = False

    def sleep(self):
        """Deep sleep, for shutdown or long idle; the next frame resets the panel."""
        if not self.sleeping:
            self.power_off()
            self._command(0x07, 0xA5)
            self.sleeping = True

    def close(self):
        self.sleep()
        self._spi.close()
        for device in (self._rst, self._dc, self._busy):
            device.close()
