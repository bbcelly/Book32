"""GPIO push buttons with short- and long-press callbacks (gpiozero)."""

import logging
from typing import Callable, Dict


class GPIOButtons:
    """Buttons from the `buttons` section of config.yaml. A press shorter than
    `long_press` seconds fires the short callback on release; holding fires
    the long callback once, while still held."""

    def __init__(self, buttons_config: Dict[str, dict]):
        self.logger = logging.getLogger(__name__)
        self.buttons = {}
        self._held = set()
        self._short: Dict[str, Callable] = {}
        self._long: Dict[str, Callable] = {}
        try:
            from gpiozero import Button
        except ImportError:
            self.logger.warning("gpiozero not available: GPIO buttons disabled")
            return
        for name, cfg in (buttons_config or {}).items():
            try:
                button = Button(cfg["pin"], pull_up=cfg.get("pull", "up") == "up",
                                bounce_time=cfg.get("bounce_time", 0.2),
                                hold_time=cfg.get("long_press", 0.8))
            except Exception as e:
                self.logger.error(f"Button '{name}' on GPIO {cfg.get('pin')}: {e}")
                continue
            button.when_pressed = lambda n=name: self._held.discard(n)
            button.when_held = lambda n=name: self._on_held(n)
            button.when_released = lambda n=name: self._on_released(n)
            self.buttons[name] = button
            self.logger.info(f"Button '{name}' on GPIO {cfg['pin']}")

    def on_press(self, name: str, short: Callable, long: Callable):
        self._short[name] = short
        self._long[name] = long

    def _on_held(self, name):
        self._held.add(name)
        if name in self._long:
            self._long[name]()

    def _on_released(self, name):
        if name not in self._held and name in self._short:
            self._short[name]()
        self._held.discard(name)

    def cleanup(self):
        for button in self.buttons.values():
            button.close()
