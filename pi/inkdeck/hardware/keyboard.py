"""Bluetooth or USB keyboard input via evdev.

Keys map to navigation commands (see KEY_COMMANDS). While a text app such as
the Typewriter is open, `raw_key_callback` receives every key press instead,
as (key_code, character or None, modifiers).
"""

import logging
import select
import threading
import time
from typing import Callable, Optional

try:
    import evdev
    from evdev import ecodes
except ImportError:
    evdev = None
    ecodes = None


def _key_maps():
    if ecodes is None:
        return {}, {}
    e = ecodes
    commands = {
        e.KEY_UP: "prev", e.KEY_LEFT: "prev", e.KEY_PAGEUP: "prev",
        e.KEY_DOWN: "next", e.KEY_RIGHT: "next", e.KEY_PAGEDOWN: "next", e.KEY_SPACE: "next",
        e.KEY_ENTER: "select",
        e.KEY_ESC: "back", e.KEY_BACKSPACE: "back", e.KEY_Q: "back",
        e.KEY_HOME: "home", e.KEY_H: "home",
    }
    chars = {getattr(e, f"KEY_{c}"): (c.lower(), c) for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"}
    chars.update({getattr(e, f"KEY_{d}"): (d, s) for d, s in zip("1234567890", "!@#$%^&*()")})
    chars.update({
        e.KEY_SPACE: (" ", " "), e.KEY_TAB: ("\t", "\t"),
        e.KEY_MINUS: ("-", "_"), e.KEY_EQUAL: ("=", "+"),
        e.KEY_LEFTBRACE: ("[", "{"), e.KEY_RIGHTBRACE: ("]", "}"),
        e.KEY_SEMICOLON: (";", ":"), e.KEY_APOSTROPHE: ("'", '"'),
        e.KEY_GRAVE: ("`", "~"), e.KEY_BACKSLASH: ("\\", "|"),
        e.KEY_COMMA: (",", "<"), e.KEY_DOT: (".", ">"), e.KEY_SLASH: ("/", "?"),
    })
    return commands, chars


KEY_COMMANDS, KEY_CHARS = _key_maps()
_IGNORED_DEVICES = ("gpio", "ir-receiver", "power button", "hdmi", "cec")


class KeyboardHandler:
    def __init__(self, on_command: Callable[[str], None], device_pattern: Optional[str] = None):
        self.logger = logging.getLogger(__name__)
        self.on_command = on_command
        self.device_pattern = (device_pattern or "").lower()
        self.raw_key_callback: Optional[Callable] = None
        self.device = None
        self.running = False
        self.modifiers = {"shift": False, "ctrl": False, "alt": False}

    def start(self) -> bool:
        if evdev is None:
            self.logger.info("evdev not available: keyboard input disabled")
            return False
        self.running = True
        threading.Thread(target=self._run, name="keyboard", daemon=True).start()
        return True

    def stop(self):
        self.running = False
        if self.device:
            try:
                self.device.close()
            except OSError:
                pass
            self.device = None

    def _find_device(self):
        for path in evdev.list_devices():
            try:
                device = evdev.InputDevice(path)
                keys = device.capabilities().get(ecodes.EV_KEY, [])
            except OSError:
                continue
            name = device.name.lower()
            is_keyboard = ecodes.KEY_ENTER in keys or ecodes.KEY_DOWN in keys
            wanted = self.device_pattern in name if self.device_pattern else not any(
                skip in name for skip in _IGNORED_DEVICES)
            if is_keyboard and wanted:
                self.logger.info(f"Keyboard connected: {device.name}")
                return device
            device.close()
        return None

    def _run(self):
        while self.running:
            device = self.device
            if device is None:
                self.device = self._find_device()
                if self.device is None:
                    time.sleep(2)
                continue
            try:
                ready, _, _ = select.select([device.fd], [], [], 0.5)
                if ready:
                    for event in device.read():
                        if event.type == ecodes.EV_KEY:
                            self._on_key(event.code, event.value)
            except (OSError, ValueError):  # Unplugged, or closed by stop()
                if self.running:
                    self.logger.info("Keyboard disconnected")
                self.device = None
            except Exception as e:
                self.logger.error(f"Keyboard event error: {e}", exc_info=True)

    def _on_key(self, code: int, value: int):
        e = ecodes
        for name, codes in (("shift", (e.KEY_LEFTSHIFT, e.KEY_RIGHTSHIFT)),
                            ("ctrl", (e.KEY_LEFTCTRL, e.KEY_RIGHTCTRL)),
                            ("alt", (e.KEY_LEFTALT, e.KEY_RIGHTALT))):
            if code in codes:
                self.modifiers[name] = value != 0
                return
        if value != 1:  # Key down only; ignore release and auto-repeat
            return
        raw = self.raw_key_callback
        if raw:
            pair = KEY_CHARS.get(code)
            char = pair[1 if self.modifiers["shift"] else 0] if pair else None
            raw(code, char, dict(self.modifiers))
        elif code in KEY_COMMANDS:
            self.on_command(KEY_COMMANDS[code])
