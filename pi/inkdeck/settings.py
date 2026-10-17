"""User settings changed from the web interface, kept in data/settings.json."""

import logging
import threading

from .paths import data_path, load_json, save_json

DEFAULTS = {
    "zoom": 1.0,
    "full_refresh_interval": 10,
    "show_page_numbers": True,
    "items_per_page": 4,
    "wifi_while_reading": False,
    "sleep_enabled": True,
    "sleep_timeout": 120,
    "sleep_message": "Shh I'm sleeping",
    "shutdown_message": "OFF",
    "undervolt": 0,
    "boot_cores": 4,
}

# Numeric limits, and maximum lengths for text settings.
LIMITS = {
    "zoom": (0.5, 2.0),
    "full_refresh_interval": (1, 50),
    "items_per_page": (3, 6),
    "sleep_timeout": (30, 3600),
    "undervolt": (-8, 0),
    "boot_cores": (1, 4),
    "sleep_message": 50,
    "shutdown_message": 20,
}


def _coerce(key, value):
    default = DEFAULTS[key]
    if isinstance(default, bool):
        if isinstance(value, str):
            return value.strip().lower() in ("1", "true", "on", "yes")
        return bool(value)
    if isinstance(default, (int, float)):
        number = type(default)(float(value))
        low, high = LIMITS[key]
        return min(max(number, low), high)
    return str(value)[: LIMITS.get(key, 200)]


class Settings:
    def __init__(self, path=None):
        self.path = path or data_path("settings.json")
        self.logger = logging.getLogger(__name__)
        self._lock = threading.Lock()
        self._values = dict(DEFAULTS)
        stored = load_json(self.path, {})
        for key, value in stored.items() if isinstance(stored, dict) else ():
            if key in DEFAULTS:
                try:
                    self._values[key] = _coerce(key, value)
                except (TypeError, ValueError):
                    self.logger.warning(f"Ignoring invalid setting {key}={value!r}")

    def get(self, key):
        with self._lock:
            return self._values[key]

    def all(self) -> dict:
        with self._lock:
            return dict(self._values)

    def update(self, changes: dict) -> dict:
        """Validate and save known settings; returns the ones that changed."""
        with self._lock:
            changed = {}
            for key, value in changes.items():
                if key not in DEFAULTS:
                    continue
                new = _coerce(key, value)
                if new != self._values[key]:
                    self._values[key] = new
                    changed[key] = new
            if changed:
                save_json(self.path, self._values)
                self.logger.info(f"Settings changed: {changed}")
            return changed
