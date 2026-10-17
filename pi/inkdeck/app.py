"""InkDeck for Raspberry Pi: the application.

Every input (the GPIO button, the PiSugar button, a keyboard and the web
interface's remote) becomes one of these commands, handled by InkDeckApp.handle:

  next    Next page, item or app (short press)
  prev    Previous page, item or app
  select  Open the selected app or book; elsewhere, return to the menu (long press)
  toggle  Like select, but the reader returns to the library and To Do checks
          off the selected task (PiSugar long tap)
  back    Reader to library; anything else to the menu
  home    Main menu

Input arrives on several threads; a lock lets one command (and its screen
update) finish before the next starts.
"""

import gc
import logging
import logging.handlers
import os
import signal
import subprocess
import sys
import threading
import time

from . import VERSION
from .apps.ipscanner import IPScannerScreen
from .apps.klipper import KlipperScreen
from .apps.todo import ToDoScreen, TodoStore
from .apps.typewriter import TypewriterScreen
from .config import Config
from .display import DisplayDriver
from .hardware.battery import BatteryMonitor
from .hardware.buttons import GPIOButtons
from .paths import SCRIPTS_DIR, resolve
from .power import PowerManager, boot_cores, boot_undervolt
from .reader.progress import ProgressManager
from .settings import Settings
from .ui.common import message_screen
from .ui.navigation import Screen
from .ui.screens import HOME_ENTRY, LibraryScreen, MainMenuScreen, ReaderScreen
from .ui.wifi_setup import WifiSetupScreen
from .wifi import WifiManager

COMMANDS = ("next", "prev", "select", "toggle", "back", "home")
# Aliases accepted from the PiSugar button and older PiBook setups.
ALIASES = {"next_page": "next", "prev_page": "prev", "menu": "home"}
FOOTER_SETTINGS = ("show_chapter", "show_page_numbers", "show_percentage")


def setup_logging(config: Config):
    handlers = []
    if config.get("logging.console", True):
        handlers.append(logging.StreamHandler())
    log_file = config.get("logging.file")
    if log_file:
        path = resolve(log_file)
        path.parent.mkdir(parents=True, exist_ok=True)
        handlers.append(logging.handlers.RotatingFileHandler(
            path, maxBytes=int(config.get("logging.max_bytes", 2_000_000)), backupCount=2))
    logging.basicConfig(level=getattr(logging, str(config.get("logging.level", "INFO")).upper(), logging.INFO),
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s", handlers=handlers)
    for noisy in ("PIL", "werkzeug"):
        logging.getLogger(noisy).setLevel(logging.WARNING)


class InkDeckApp:
    def __init__(self, config: Config):
        self.config = config
        self.logger = logging.getLogger("inkdeck")
        self.logger.info(f"InkDeck {VERSION} for Raspberry Pi starting")
        self.settings = Settings()
        self._sync_boot_settings()
        self.lock = threading.RLock()
        self.running = False

        width = config.get("display.width", 480)
        height = config.get("display.height", 800)
        self.books_dir = str(resolve(config.get("library.books_directory", "books")))
        os.makedirs(self.books_dir, exist_ok=True)
        self.web_port = config.get("web.port", 5000)

        self.display = DisplayDriver(width, height, config.get("display.rotation", 90),
                                     config.get("display.spi_hz", 10_000_000))
        self.power = PowerManager(self.logger)
        self.battery = None
        if config.get("battery.enabled", True):
            self.battery = BatteryMonitor.detect(
                pisugar_socket=config.get("pisugar.socket_path", "/tmp/pisugar-server.sock"),
                update_interval=config.get("battery.update_interval", 30),
                adc_channel=config.get("battery.adc_channel", 0),
                voltage_divider_ratio=config.get("battery.voltage_divider_ratio", 2.0),
                min_voltage=config.get("battery.min_voltage", 3.0),
                max_voltage=config.get("battery.max_voltage", 4.2))
        self.progress = ProgressManager()
        self.todos = TodoStore()

        self.main_menu = MainMenuScreen(width, height, self.battery, self.web_port)
        self.library = LibraryScreen(width, height, web_port=self.web_port, battery=self.battery)
        self.reader = ReaderScreen(width, height, cache_size=config.get("reader.page_cache_size", 3),
                                   battery=self.battery)
        self.ip_scanner = IPScannerScreen(width, height, self.battery)
        self.todo = ToDoScreen(width, height, self.todos, self.battery)
        self.klipper = KlipperScreen(width, height, self.battery)
        self.typewriter = TypewriterScreen(width, height, battery=self.battery)
        self.wifi = WifiManager(on_change=self._wifi_changed,
                                radio_wanted=lambda: self.power.wifi_on and not self.power.is_sleeping)
        self.main_menu.wifi = self.wifi
        self.wifi_setup = WifiSetupScreen(width, height, self.wifi, self.battery, self.web_port)
        self.screens = {
            Screen.MAIN_MENU: self.main_menu, Screen.LIBRARY: self.library, Screen.IP_SCANNER: self.ip_scanner,
            Screen.TODO: self.todo, Screen.KLIPPER: self.klipper, Screen.TYPEWRITER: self.typewriter,
            Screen.WIFI_SETUP: self.wifi_setup,
        }
        self.current = Screen.MAIN_MENU
        self.shown = None  # Screen on the panel; a change of screen gets a full refresh
        self.apply_settings()

        self.buttons = None
        self.pisugar_button = None
        self.keyboard = None
        self.web = None

    # ---- Settings ----

    def apply_settings(self, changed=None):
        """Push settings into the screens and power manager."""
        s = self.settings.all()
        self.display.set_full_refresh_interval(s["full_refresh_interval"])
        self.power.sleep_enabled = s["sleep_enabled"]
        self.power.sleep_timeout = s["sleep_timeout"]
        self.library.sleep_enabled = s["sleep_enabled"]
        self.library.items_per_page = s["items_per_page"]
        for key in FOOTER_SETTINGS:
            setattr(self.reader, key, s[key])
        layout = {"zoom_factor": s["zoom"], "font_family": s["font_family"], "book_fonts": s["book_fonts"]}
        with self.lock:
            relayout = self.reader.is_open and any(getattr(self.reader, k) != v for k, v in layout.items())
            for key, value in layout.items():
                setattr(self.reader, key, value)
            if changed is None:
                return
            if relayout:  # Lay the open book out again, at the same place
                path, fraction = self.reader.current_book_path, self.reader.current_page / max(1, self.reader.page_count)
                self._open_book_path(path, start_fraction=fraction)
            else:
                if self.current in (Screen.LIBRARY, Screen.READER):
                    self.render()

    def _sync_boot_settings(self):
        """Show what the boot files really say: they can be edited by hand, and a
        duplicate over_voltage line once made the saved value wrong."""
        actual = {key: value for key, value in (("undervolt", boot_undervolt()), ("boot_cores", boot_cores()))
                  if value is not None}
        changed = self.settings.update(actual)
        if changed:
            self.logger.info(f"Settings updated from the boot files: {changed}")

    def update_settings(self, changes: dict) -> dict:
        """Save settings from the web interface. Returns warnings for boot settings
        that could not be written."""
        changed = self.settings.update(changes)
        self.apply_settings(changed)
        warnings = {}
        if "undervolt" in changed and not self._root_script("apply_undervolt.sh", changed["undervolt"]):
            warnings["undervolt_warning"] = "Could not update /boot/firmware/config.txt"
        if "boot_cores" in changed and not self._root_script("apply_cpu_cores.sh", changed["boot_cores"]):
            warnings["boot_cores_warning"] = "Could not update /boot/firmware/cmdline.txt"
        return warnings

    def _root_script(self, name, value) -> bool:
        try:
            result = subprocess.run(["sudo", "-n", str(SCRIPTS_DIR / name), str(value)],
                                    capture_output=True, text=True, timeout=15)
        except (OSError, subprocess.SubprocessError) as e:
            self.logger.error(f"{name}: {e}")
            return False
        if result.returncode != 0:
            self.logger.error(f"{name} failed: {result.stderr.strip() or result.stdout.strip()}")
        return result.returncode == 0

    # ---- Life cycle ----

    def start(self):
        self.display.initialize()
        self.library.load_books(self.books_dir)

        self.buttons = GPIOButtons(self.config.get("buttons", {}))
        self.buttons.on_press("toggle", short=lambda: self.handle("next"), long=lambda: self.handle("select"))
        if self.config.get("pisugar.button_enabled", True):
            from .hardware.pisugar_button import PiSugarButtonServer
            self.pisugar_button = PiSugarButtonServer(
                self.config.get("pisugar.button_socket_path", "/tmp/inkdeck-button.sock"), self.handle)
            self.pisugar_button.start()
        if self.config.get("keyboard.enabled", True):
            from .hardware.keyboard import KeyboardHandler
            self.keyboard = KeyboardHandler(self.handle, self.config.get("keyboard.device_pattern"))
            self.keyboard.start()

        from .web.server import WebServer
        self.web = WebServer(self, self.web_port)
        self.web.start()
        self.wifi.start()

        self.running = True
        threading.Thread(target=self._monitor, name="monitor", daemon=True).start()
        self.render()
        self.logger.info(f"Ready; web interface on port {self.web_port}")

        signal.signal(signal.SIGTERM, lambda *_: self.stop())
        signal.signal(signal.SIGINT, lambda *_: self.stop())
        while self.running:
            time.sleep(1)

    def stop(self):
        if not self.running:
            return
        self.logger.info("Stopping")
        self.running = False
        self._save_position()
        for part in (self.pisugar_button, self.keyboard):
            if part:
                part.stop()
        if self.buttons:
            self.buttons.cleanup()
        self.display.cleanup()

    # ---- Commands ----

    def handle(self, command: str):
        """Run one input command (see the module docstring)."""
        command = ALIASES.get(command, command)
        if command not in COMMANDS:
            self.logger.warning(f"Unknown command: {command}")
            return
        with self.lock:
            if not self.running:
                return
            self.power.reset_activity()
            if self.power.is_sleeping:
                self._wake()
                return
            self.logger.debug(f"{command} on {self.current.value}")
            if self.current == Screen.WIFI_SETUP:  # Any button leaves the setup steps
                self.show(Screen.MAIN_MENU)
                return
            getattr(self, f"_on_{command}")()

    def _on_next(self):
        screen = self.current
        if screen == Screen.MAIN_MENU:
            self.main_menu.next_app()
        elif screen == Screen.LIBRARY:
            self.library.next_item()
        elif screen == Screen.READER:
            self._turn_page(self.reader.next_page)
            return
        elif screen == Screen.TODO:
            self.todo.next_item()
        elif screen in (Screen.IP_SCANNER, Screen.KLIPPER):
            app = self.screens[screen]
            if app.scanning:
                return
            if not (app.devices if screen == Screen.IP_SCANNER else app.printers):
                app.start_scan()
            else:
                app.next_page()
        self.render()

    def _on_prev(self):
        screen = self.current
        if screen == Screen.MAIN_MENU:
            self.main_menu.prev_app()
        elif screen == Screen.LIBRARY:
            self.library.prev_item()
        elif screen == Screen.READER:
            self._turn_page(self.reader.prev_page)
            return
        elif screen == Screen.TODO:
            self.todo.prev_item()
        elif screen in (Screen.IP_SCANNER, Screen.KLIPPER):
            self.screens[screen].prev_page()
        self.render()

    def _on_select(self):
        if self.current == Screen.MAIN_MENU:
            self._launch(self.main_menu.selected_target())
        elif self.current == Screen.LIBRARY:
            self._open_selected_book()
        else:
            self.show(Screen.MAIN_MENU)

    def _on_toggle(self):
        if self.current == Screen.READER:
            self.show(Screen.LIBRARY)
        elif self.current == Screen.TODO:
            self.todo.toggle_todo()
            self.render()
        else:
            self._on_select()

    def _on_back(self):
        self.show(Screen.LIBRARY if self.current == Screen.READER else Screen.MAIN_MENU)

    def _on_home(self):
        if self.current != Screen.MAIN_MENU:
            self.show(Screen.MAIN_MENU)

    def _launch(self, target):
        if target == "shutdown":
            self.shutdown()
        else:
            self.show(target)

    # ---- Screens ----

    def show(self, screen: Screen):
        """Switch screens: leaving the reader saves the position and restores
        Wi-Fi and CPU cores; the Typewriter takes raw keyboard input."""
        with self.lock:
            if self.current == Screen.READER and screen != Screen.READER:
                self._save_position()
                self.reader.close()
                self._leave_reading_mode()
            if self.keyboard:
                self.keyboard.raw_key_callback = self._typewriter_key if screen == Screen.TYPEWRITER else None
            if screen == Screen.LIBRARY:
                self.library.load_books(self.books_dir)
            self.current = screen
            self.render()

    def render(self, partial: bool = None):
        """Draw the current screen. Changing screens uses a full refresh."""
        with self.lock:
            try:
                if self.current == Screen.READER:
                    image = self.reader.get_current_image()
                else:
                    image = self.screens[self.current].render()
                if partial is None:
                    partial = self.shown == self.current
                if self.shown != self.current:
                    self.display.reset_partial_counter()
                self.shown = self.current
                self.display.display_image(image, use_partial=partial,
                                           count_page_turn=self.current == Screen.READER)
            except Exception as e:
                self.logger.error(f"Drawing {self.current.value} failed: {e}", exc_info=True)

    def _typewriter_key(self, key_code, char, modifiers):
        from .hardware.keyboard import ecodes
        if key_code == ecodes.KEY_ESC:
            self.handle("home")
            return
        with self.lock:
            self.power.reset_activity()
            if self.power.is_sleeping:
                self._wake()
                return
            if modifiers.get("alt"):
                self.typewriter.toggle_mode()
            else:
                self.typewriter.handle_key(key_code, char, modifiers)
            self.render(partial=True)

    # ---- Reading ----

    def _open_selected_book(self):
        book = self.library.get_selected_book()
        if not book:
            return
        if book["path"] == HOME_ENTRY:
            self.show(Screen.MAIN_MENU)
        else:
            self._open_book_path(book["path"])

    def _open_book_path(self, path: str, start_fraction: float = None):
        shown = {"percent": -100.0, "time": 0.0}

        def progress(percent, message):
            # Each e-paper update takes about a second, so update only every 10%.
            if percent - shown["percent"] >= 10 and time.monotonic() - shown["time"] >= 2:
                shown.update(percent=percent, time=time.monotonic())
                self.display.display_image(self.reader.loading_image(percent, message), use_partial=True)

        try:
            self.display.display_image(self.reader.loading_image(0, "Opening book..."), use_partial=False)
            self.reader.load_epub(path, progress_callback=progress)
        except Exception as e:
            self.logger.error(f"Could not open {os.path.basename(path)}: {e}", exc_info=True)
            self.reader.close()
            self.shown = None
            self.show(Screen.LIBRARY)
            return
        if start_fraction is not None:
            self.reader.go_to_page(round(start_fraction * self.reader.page_count))
        else:
            self.reader.go_to_page(self.progress.load_progress(path, self.reader.page_count) or 0)
        self._enter_reading_mode()
        self.current = Screen.READER
        self.shown = None
        self.render()
        self.reader.prerender_next()

    def _turn_page(self, turn):
        if turn():
            self._save_position()
            self.render()
            if self.config.get("performance.gc_on_page_turn", True):
                gc.collect()
            self.reader.prerender_next()

    def _save_position(self):
        if self.reader.is_open:
            self.progress.save_progress(self.reader.current_book_path, self.reader.current_page,
                                        self.reader.page_count, self.reader.chapter_title())

    def _wifi_may_turn_off(self) -> bool:
        """Wi-Fi turns off while reading and asleep, except when the user keeps
        it on, and never while the setup hotspot is up."""
        return not self.settings.get("wifi_while_reading") and not self.wifi.setup_active

    def _enter_reading_mode(self):
        if self._wifi_may_turn_off():
            self.power.set_wifi(False)
        if self.config.get("power.single_core_reading", True):
            self.power.set_online_cores(1)

    def _leave_reading_mode(self):
        if not self.settings.get("wifi_while_reading"):
            self.power.set_wifi(True)
        if self.config.get("power.single_core_reading", True):
            self.power.set_online_cores(self.settings.get("boot_cores"))

    # ---- Wi-Fi setup ----

    def _wifi_changed(self, event: str):
        """The setup hotspot started, or a network is being joined, joined or
        failed (see wifi.py). Shows the setup steps unless a book or the
        Terminal is open; the main menu shows a reminder either way."""
        with self.lock:
            if not self.running or self.power.is_sleeping:
                return
            if self.current in (Screen.READER, Screen.TYPEWRITER):
                return
            if event == "hotspot" or self.current == Screen.WIFI_SETUP:
                self.show(Screen.WIFI_SETUP)
            elif self.current == Screen.MAIN_MENU:
                self.render()

    # ---- Sleep and shutdown ----

    def _sleep(self):
        with self.lock:
            self.logger.info("Sleeping after inactivity")
            self._save_position()
            self.power.is_sleeping = True
            self.display.display_image(message_screen(self.display.width, self.display.height,
                                                      self.settings.get("sleep_message")), use_partial=False)
            self.shown = None
            if self._wifi_may_turn_off():
                self.power.set_wifi(False)

    def _wake(self):
        self.logger.info("Waking")
        self.power.is_sleeping = False
        if self.current != Screen.READER and not self.settings.get("wifi_while_reading"):
            self.power.set_wifi(True)  # Wi-Fi stays off while a book is open
        self.render()

    def shutdown(self):
        """Show the shutdown message, then power the Pi off."""
        with self.lock:
            self.logger.info("Shutting down")
            self._save_position()
            self.display.display_image(message_screen(self.display.width, self.display.height,
                                                      self.settings.get("shutdown_message")), use_partial=False)
            self.stop()
            subprocess.Popen(["sudo", "-n", "shutdown", "-h", "now"])

    # ---- Background work ----

    def _monitor(self):
        """Once a second: sleep after inactivity, redraw scanners while they
        work, refresh Klipper status, and redraw when the battery changes."""
        last_battery, was_busy = None, False
        last_battery_check = 0.0
        while self.running:
            time.sleep(1)
            try:
                self.display.idle()
                if self.power.should_sleep() and self.current != Screen.WIFI_SETUP:
                    self._sleep()
                    continue
                if self.power.is_sleeping:
                    continue
                if self.current == Screen.KLIPPER and self.klipper.refresh_due():
                    self.klipper.refresh_all()
                app = self.screens.get(self.current)
                busy = bool(app and (getattr(app, "scanning", False) or getattr(app, "refreshing", False)))
                if busy or was_busy:
                    self.render(partial=True)  # Progress while busy, then the result
                was_busy = busy

                if self.battery and time.monotonic() - last_battery_check >= 300:
                    last_battery_check = time.monotonic()
                    self.battery.force_update()
                    reading = (self.battery.percentage // 5, self.battery.charging)
                    if last_battery is not None and reading != last_battery and self.current != Screen.READER:
                        self.render(partial=True)
                    last_battery = reading
            except Exception as e:
                self.logger.error(f"Monitor error: {e}", exc_info=True)


def main():
    config_path = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("INKDECK_CONFIG")
    config = Config(config_path)
    setup_logging(config)
    InkDeckApp(config).start()


if __name__ == "__main__":
    main()
