"""Battery level from a PiSugar 2 (via pisugar-server) or an ADS1115 ADC.

BatteryMonitor.detect() returns None when neither is present, so screens show
no battery rather than a made-up level.
"""

import logging
import os
import socket
import time
from collections import deque
from datetime import datetime
from typing import Optional


class PiSugar2Backend:
    name = "PiSugar2"

    def __init__(self, socket_path: str = "/tmp/pisugar-server.sock"):
        self.logger = logging.getLogger(__name__)
        self.socket_path = socket_path

    def is_available(self) -> bool:
        return os.path.exists(self.socket_path)

    def _query(self, command: str) -> Optional[str]:
        """Value after the colon in pisugar-server's reply, e.g. 'battery: 75.5'."""
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
                sock.settimeout(1.0)
                sock.connect(self.socket_path)
                sock.sendall(f"{command}\n".encode())
                reply = sock.recv(1024).decode().strip()
            return reply.split(":", 1)[1].strip() if ":" in reply else None
        except OSError as e:
            self.logger.debug(f"PiSugar query '{command}' failed: {e}")
            return None

    def _number(self, command: str) -> Optional[float]:
        try:
            return float(self._query(command))
        except (TypeError, ValueError):
            return None

    def read_voltage(self) -> float:
        return self._number("get battery_v") or 0.0

    def read_percentage(self) -> int:
        return int(self._number("get battery") or 0)

    def is_charging(self) -> bool:
        # battery_power_plugged is reliable; battery_i reads inverted on some units.
        plugged = self._query("get battery_power_plugged")
        if plugged is not None:
            return plugged.lower() == "true"
        current = self._number("get battery_i")
        return current is not None and current > 0.01

    def rtc_time(self) -> Optional[datetime]:
        value = self._query("get rtc_time")
        try:
            return datetime.fromisoformat(value[:19]) if value else None
        except ValueError:
            return None


class ADS1115Backend:
    name = "ADS1115"

    def __init__(self, adc_channel=0, voltage_divider_ratio=2.0, min_voltage=3.0, max_voltage=4.2):
        self.ratio = voltage_divider_ratio
        self.min_voltage = min_voltage
        self.max_voltage = max_voltage
        self.channel = None
        try:
            import board
            import busio
            import adafruit_ads1x15.ads1115 as ADS
            from adafruit_ads1x15.analog_in import AnalogIn
            ads = ADS.ADS1115(busio.I2C(board.SCL, board.SDA))
            pins = {0: ADS.P0, 1: ADS.P1, 2: ADS.P2, 3: ADS.P3}
            self.channel = AnalogIn(ads, pins[adc_channel])
        except Exception as e:
            logging.getLogger(__name__).debug(f"ADS1115 not available: {e}")

    def is_available(self) -> bool:
        return self.channel is not None

    def read_voltage(self) -> float:
        try:
            return self.channel.voltage * self.ratio
        except Exception:
            return 0.0

    def percentage_for(self, voltage: float) -> int:
        voltage = min(max(voltage, self.min_voltage), self.max_voltage)
        return round((voltage - self.min_voltage) / (self.max_voltage - self.min_voltage) * 100)

    def read_percentage(self) -> int:
        return self.percentage_for(self.read_voltage())

    def is_charging(self) -> bool:
        return False  # An ADC on the battery cannot tell


class BatteryMonitor:
    """Cached, smoothed readings from one backend, refreshed every update_interval."""

    def __init__(self, backend, update_interval: float = 30.0, smoothing_samples: int = 5):
        self.logger = logging.getLogger(__name__)
        self.backend = backend
        self.update_interval = update_interval
        self.voltages = deque(maxlen=smoothing_samples)
        self.last_update = 0.0
        self.voltage = 0.0
        self.percentage = 0
        self.charging = False
        self.force_update()

    @classmethod
    def detect(cls, pisugar_socket="/tmp/pisugar-server.sock", update_interval=30.0, **ads_options):
        pisugar = PiSugar2Backend(pisugar_socket)
        if pisugar.is_available():
            backend = pisugar
        else:
            backend = ADS1115Backend(**ads_options)
            if not backend.is_available():
                logging.getLogger(__name__).info("No battery hardware found")
                return None
        logging.getLogger(__name__).info(f"Battery backend: {backend.name}")
        return cls(backend, update_interval)

    def force_update(self):
        self.voltages.append(self.backend.read_voltage())
        self.voltage = sum(self.voltages) / len(self.voltages)
        if isinstance(self.backend, ADS1115Backend):
            self.percentage = self.backend.percentage_for(self.voltage)
        else:
            self.percentage = self.backend.read_percentage()
        self.charging = self.backend.is_charging()
        self.last_update = time.monotonic()

    def _refresh(self):
        if time.monotonic() - self.last_update >= self.update_interval:
            self.force_update()

    def get_percentage(self) -> int:
        self._refresh()
        return self.percentage

    def get_voltage(self) -> float:
        self._refresh()
        return self.voltage

    def is_charging(self) -> bool:
        self._refresh()
        return self.charging

    def get_status(self) -> dict:
        self._refresh()
        return {
            "voltage": round(self.voltage, 3),
            "percentage": self.percentage,
            "is_charging": self.charging,
            "is_low": self.percentage <= 20,
            "backend": self.backend.name,
        }

    def get_time(self) -> datetime:
        """The system clock once it has been set (by NTP), else the PiSugar RTC."""
        now = datetime.now()
        if now.year < 2025 and isinstance(self.backend, PiSugar2Backend):
            return self.backend.rtc_time() or now
        return now
