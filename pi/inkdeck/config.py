"""Hardware and system configuration from config/config.yaml.

config/local.yaml, when present, overrides individual values. The app never
writes either file: settings changed at run time live in data/settings.json.
"""

import logging
from pathlib import Path
from typing import Any

import yaml

from .paths import APP_ROOT

DEFAULT_PATH = APP_ROOT / "config" / "config.yaml"


def _load(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f) or {}


def _merge(base: dict, override: dict):
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(base.get(key), dict):
            _merge(base[key], value)
        else:
            base[key] = value


class Config:
    def __init__(self, path=None):
        path = Path(path) if path else DEFAULT_PATH
        self.path = path
        self._config = _load(path)
        local = path.with_name("local.yaml")
        if local.exists():
            _merge(self._config, _load(local))
        logging.getLogger(__name__).info(f"Configuration loaded from {path}")

    def get(self, dotted: str, default: Any = None) -> Any:
        """A value by dotted path, such as 'display.width'."""
        value = self._config
        for key in dotted.split("."):
            if not isinstance(value, dict) or key not in value:
                return default
            value = value[key]
        return value
