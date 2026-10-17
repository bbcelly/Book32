"""InkDeck for Raspberry Pi tests. They run on any computer: without the
Waveshare library, gpiozero or a battery, the app runs in mock mode."""

import json
import shutil

import pytest
from conftest import make_epub

from inkdeck import paths
from inkdeck.apps.todo import TodoStore
from inkdeck.config import Config
from inkdeck.hardware.battery import ADS1115Backend, BatteryMonitor
from inkdeck.power import count_cpus
from inkdeck.reader.covers import CoverExtractor
from inkdeck.reader.progress import ProgressManager
from inkdeck.reader.renderer import LAYOUT_DIR, PillowTextRenderer, forget_layouts
from inkdeck.settings import Settings
from inkdeck.ui.common import message_screen, wrap_text
from inkdeck.ui.navigation import Screen
from inkdeck.ui.screens import ReaderScreen
from inkdeck.ui.wifi_setup import WifiSetupScreen
from inkdeck.wifi import SETUP_SSID, WifiManager, parse_scan, password_error, split_terse


# ---- Storage ----

def test_settings_validate_and_persist(tmp_path):
    path = tmp_path / "settings.json"
    settings = Settings(path)
    changed = settings.update({"zoom": "5", "items_per_page": 2, "sleep_enabled": "false",
                               "sleep_message": "x" * 80, "unknown": 1})
    assert changed["zoom"] == 2.0 and changed["items_per_page"] == 3
    assert changed["sleep_enabled"] is False and len(changed["sleep_message"]) == 50
    assert "unknown" not in json.loads(path.read_text())
    assert Settings(path).get("zoom") == 2.0
    assert settings.update({"zoom": 2.0}) == {}  # Unchanged values are not saved again


def test_progress_converts_old_path_keys(tmp_path):
    path = tmp_path / "progress.json"
    path.write_text(json.dumps({"/home/pi/PiBook/books/A.epub": {"current_page": 41, "total_pages": 300}}))
    progress = ProgressManager(path)
    assert progress.load_progress("/home/someone/InkDeck/pi/books/A.epub") == 41
    assert progress.load_progress("A.epub", total_pages=600) == 82  # Same place after a relayout
    progress.save_progress("books/B.epub", 3, 10)
    assert set(json.loads(path.read_text())) == {"A.epub", "B.epub"}


def test_todo_store_adds_ids_to_old_tasks(tmp_path):
    path = tmp_path / "todos.json"
    path.write_text(json.dumps({"tasks": [{"text": "old", "completed": False}]}))
    store = TodoStore(path)
    old = store.tasks()[0]
    assert old["id"] and store.toggle(old["id"])["completed"] is True
    new = store.add("new")
    assert store.edit(new["id"], "renamed")["text"] == "renamed"
    assert store.delete(old["id"]) and [t["text"] for t in store.tasks()] == ["renamed"]
    assert store.toggle("missing") is None


def test_save_json_is_atomic(tmp_path):
    target = tmp_path / "file.json"
    paths.save_json(target, {"a": 1})
    assert paths.load_json(target, None) == {"a": 1} and not list(tmp_path.glob("*.tmp"))
    assert paths.load_json(tmp_path / "missing.json", "default") == "default"


def test_config_local_overrides(tmp_path):
    (tmp_path / "config.yaml").write_text("web:\n  port: 5000\ndisplay:\n  width: 480\n")
    (tmp_path / "local.yaml").write_text("web:\n  port: 8080\n")
    config = Config(tmp_path / "config.yaml")
    assert config.get("web.port") == 8080 and config.get("display.width") == 480
    assert config.get("missing.key", 7) == 7


# ---- Hardware helpers ----

def test_cpu_lists():
    assert count_cpus("0-3") == 4 and count_cpus("0") == 1 and count_cpus("0,2-3") == 3


def test_battery_monitor_smooths_ads_voltage():
    class FakeAds(ADS1115Backend):
        def __init__(self):
            self.ratio, self.min_voltage, self.max_voltage, self.channel = 1, 3.0, 4.2, object()  # No I2C
            self.readings = iter([3.6, 3.6, 4.2])

        def read_voltage(self):
            return next(self.readings)

    monitor = BatteryMonitor(FakeAds(), update_interval=3600)
    assert monitor.get_percentage() == 50
    monitor.force_update()
    monitor.force_update()
    assert monitor.get_voltage() == pytest.approx(3.8) and monitor.get_percentage() == 67
    assert monitor.get_status()["backend"] == "ADS1115"


# ---- Reader ----

def test_renderer_paginates_caches_and_draws(epub):
    renderer = PillowTextRenderer(str(epub), 480, 800)
    assert renderer.page_count > 3
    first = renderer.render_page(0)
    assert first.size == (480, 800) and first.mode == "1"
    assert any(len(item) == 6 and item[3] == "IMAGE" for page in renderer.pages for item in page)
    assert any(len(item) == 6 and item[3] == "TABLE" for page in renderer.pages for item in page)
    assert list(LAYOUT_DIR.glob("*.pickle"))
    cached = PillowTextRenderer(str(epub), 480, 800)
    assert cached.pages == renderer.pages
    bigger = PillowTextRenderer(str(epub), 480, 800, zoom_factor=1.5)
    assert bigger.page_count > renderer.page_count
    forget_layouts(str(epub))
    assert not list(LAYOUT_DIR.glob("*.pickle"))


def test_chapters_from_table_of_contents(epub):
    renderer = PillowTextRenderer(str(epub), 480, 800)
    titles = [c.title for c in renderer.chapters]
    assert titles == ["Chapter 1", "Interlude", "Chapter 2", "Chapter 3"]
    pages = [c.page for c in renderer.chapters]
    assert pages[0] == 0 and pages == sorted(pages) and len(set(pages)) == 4
    # Spine order: chapter 1's heading is the first text, though the manifest lists it last.
    assert renderer.pages[0][0][2] == "Chapter"
    interlude = renderer.chapters[1].page
    assert any(item[2] == "Interlude" for item in renderer.pages[interlude] if len(item) == 4)
    assert renderer.chapter_at(interlude - 1).title == "Chapter 1"
    assert renderer.chapter_at(interlude).title == "Interlude"
    assert renderer.chapter_at(renderer.page_count - 1).title == "Chapter 3"
    assert [c.title for c in PillowTextRenderer(str(epub), 480, 800).chapters] == titles  # From the cache


def test_reader_footer(epub):
    reader = ReaderScreen(480, 800)
    reader.load_epub(str(epub))
    reader.go_to_page(reader.chapters[2].page)
    assert reader.chapter_title() == "Chapter 2" and 0 < reader.percent_read() < 100
    footer = (0, 770, 480, 800)
    shown = reader.get_current_image().crop(footer)
    assert shown.getextrema()[0] == 0  # Something drawn
    reader.show_chapter = reader.show_page_numbers = reader.show_percentage = False
    assert reader.get_current_image().crop(footer).getextrema()[0] > 0  # All white


def test_boot_settings(tmp_path):
    from inkdeck.power import boot_cores, boot_undervolt
    assert boot_undervolt([tmp_path]) is None and boot_cores([tmp_path]) is None
    (tmp_path / "config.txt").write_text("[all]\nover_voltage=-6\n#over_voltage=-8\n[all]\nover_voltage=-2\n")
    (tmp_path / "cmdline.txt").write_text("console=tty1 root=PARTUUID=x rootwait maxcpus=1\n")
    assert boot_undervolt([tmp_path]) == -2 and boot_cores([tmp_path]) == 1
    (tmp_path / "config.txt").write_text("dtparam=spi=on\n")
    (tmp_path / "cmdline.txt").write_text("console=tty1\n")
    assert boot_undervolt([tmp_path]) == 0 and boot_cores([tmp_path]) == 4


def test_reading_fonts(epub):
    open_sans = PillowTextRenderer(str(epub), 480, 800, font_family="openSans", book_fonts=False)
    assert "Open Sans" in open_sans.fonts["normal"].getname()[0]
    assert open_sans.fonts["bold"].getname()[1] == "Bold"
    serif = PillowTextRenderer(str(epub), 480, 800, font_family="serif")
    assert open_sans._cache_path() != serif._cache_path()
    assert PillowTextRenderer(str(epub), 480, 800, font_family="nope").font_family == "serif"
    with pytest.raises(ValueError):
        Settings(paths.data_path("font-test.json")).update({"font_family": "Comic Sans"})


def test_panel_driver_commands(monkeypatch):
    """The fast driver's command sequence, against fake SPI and GPIO."""
    import sys
    import types

    from inkdeck.hardware import epd7in5

    sent = []

    class Pin:
        def __init__(self, pin, **kwargs):
            self.pin, self.value = pin, 1

        def on(self):
            self.value = 1
            if self.pin == epd7in5.RST_PIN:
                sent.append("reset")

        def off(self):
            self.value = 0

        def close(self):
            pass

    class Spi:
        def open(self, bus, device):
            pass

        def writebytes(self, data):
            sent.append(("dc", dc.value, list(data)))

        def writebytes2(self, data):
            sent.append(("frame", bytes(data)))

        def close(self):
            pass

    monkeypatch.setitem(sys.modules, "gpiozero", types.SimpleNamespace(DigitalOutputDevice=Pin, DigitalInputDevice=Pin))
    monkeypatch.setitem(sys.modules, "spidev", types.SimpleNamespace(SpiDev=Spi))
    monkeypatch.setattr(epd7in5.time, "sleep", lambda s: None)
    panel = epd7in5.EPD7in5()
    dc = panel._dc
    frame = bytes(range(256)) * (epd7in5.FRAME_BYTES // 256) + bytes(epd7in5.FRAME_BYTES % 256)

    def commands():
        return [e[2][0] for e in sent if e[0] == "dc" and e[1] == 0]

    panel.display(frame, full=False)  # The first frame is always full
    assert ("frame", bytes([255]) * epd7in5.FRAME_BYTES) in sent and ("frame", frame) in sent
    assert ("dc", 1, [0x29, 0x07]) in sent and ("dc", 1, [epd7in5.FULL_TEMPERATURE]) in sent
    assert commands()[-1] == 0x12 and 0x91 not in commands()
    sent.clear()
    panel.display(frame, full=False)
    assert "reset" not in sent and 0x04 not in commands()  # No reset or power-up between pages
    assert ("dc", 1, [0xA9, 0x07]) in sent and ("dc", 1, [epd7in5.PARTIAL_TEMPERATURE]) in sent
    assert sent.count(("frame", frame)) == 1 and commands()[-2:] == [0x12, 0x92]
    sent.clear()
    panel.power_off()
    panel.display(frame, full=True)
    assert commands()[:4] == [0x02, 0x00, 0x50, 0xE5] and 0x04 in commands()
    sent.clear()
    panel.close()
    assert commands() == [0x02, 0x07]


def test_cover_from_opf_metadata(epub, tmp_path):
    covers = CoverExtractor(tmp_path / "covers")
    cover = covers.get_cover(str(epub), (100, 150))
    assert cover.size == (100, 150) and cover.getextrema()[0] == 0  # Dark pixels: the real cover
    assert list((tmp_path / "covers").glob("*.png"))
    covers.forget(str(epub))
    assert not list((tmp_path / "covers").glob("*.png"))


def test_text_helpers():
    from PIL import Image, ImageDraw
    from inkdeck.ui.common import font
    draw = ImageDraw.Draw(Image.new("1", (10, 10)))
    lines = wrap_text(draw, "word " * 60, font(20), 200, max_lines=3)
    assert len(lines) == 3 and lines[-1].endswith("…")
    assert message_screen(480, 800, "Shh I'm sleeping").size == (480, 800)


def test_wifi_scan_parsing():
    assert split_terse(r"Cafe\: Guest:72:WPA2") == ["Cafe: Guest", "72", "WPA2"]
    scan = parse_scan("\n".join(["Home:40:WPA2", "Home:81:WPA2 WPA3", "Open Cafe:55:", ":90:WPA2",
                                  f"{SETUP_SSID}:99:", "Weak:x:WPA1"]))
    assert scan == [{"ssid": "Home", "signal": 81, "secure": True}, {"ssid": "Open Cafe", "signal": 55, "secure": False},
                    {"ssid": "Weak", "signal": 0, "secure": True}]
    assert password_error("") is None and password_error("12345678") is None
    assert password_error("short") and password_error("x" * 64)


def test_wifi_setup_screen_states():
    wifi = WifiManager()
    screen = WifiSetupScreen(480, 800, wifi)
    for state, message in (("hotspot", ""), ("hotspot", "Could not join Home: check the password and try again."),
                           ("connecting", ""), ("retrying", ""), ("normal", "Connected to Home")):
        wifi.state, wifi.ssid, wifi.message = state, "Home", message
        image = screen.render()
        assert image.size == (480, 800) and image.mode == "1"
        assert image.getbbox() is not None


# ---- The app ----

@pytest.fixture
def app(tmp_path):
    books = tmp_path / "books"
    books.mkdir()
    make_epub(books / "Alpha.epub", "Alpha")
    make_epub(books / "Beta.epub", "Beta", chapters=1, image=False)
    config_file = tmp_path / "config.yaml"
    config_file.write_text(f"library:\n  books_directory: {books.as_posix()}\nweb:\n  port: 5099\n"
                           "pisugar:\n  button_enabled: false\nkeyboard:\n  enabled: false\n")
    for name in ("settings.json", "reading_progress.json", "todos.json"):
        (paths.DATA_DIR / name).unlink(missing_ok=True)
    from inkdeck.app import InkDeckApp
    instance = InkDeckApp(Config(config_file))
    instance.power.set_wifi = lambda on: None  # Never touch this computer's network
    instance.wifi.available = False
    instance.library.load_books(instance.books_dir)
    instance.running = True
    yield instance
    instance.running = False
    shutil.rmtree(books, ignore_errors=True)


def test_every_screen_draws(app):
    for screen in (Screen.MAIN_MENU, Screen.LIBRARY, Screen.IP_SCANNER, Screen.TODO,
                   Screen.KLIPPER, Screen.TYPEWRITER):
        image = app.screens[screen].render()
        assert image.size == (480, 800) and image.mode == "1", screen
    app.todos.add("A task long enough to wrap onto a second line on the e-paper screen, and more")
    assert app.todo.render().size == (480, 800)


def test_reading_flow_saves_position(app):
    app.handle("next")
    assert app.main_menu.selected_name() == "IP Scanner"
    app.handle("prev")
    app.handle("select")                      # Open eReader
    assert app.current == Screen.LIBRARY
    assert [b["title"] for b in app.library.books] == ["Alpha", "Beta", "Back to menu"]
    app.handle("select")                      # Open Alpha
    assert app.current == Screen.READER and app.reader.page_count > 3
    app.handle("next")
    app.handle("next")
    app.handle("prev")
    assert app.reader.current_page == 1
    app.handle("toggle")                      # PiSugar long tap: back to the library
    assert app.current == Screen.LIBRARY and not app.reader.is_open
    assert app.progress.load_progress("Alpha.epub") == 1
    app.handle("select")                      # Reopens at the saved page
    assert app.reader.current_page == 1
    app.handle("select")                      # Long press in the reader: main menu
    assert app.current == Screen.MAIN_MENU
    app.handle("menu")                        # Old alias for home
    app.handle("bogus")                       # Ignored
    assert app.current == Screen.MAIN_MENU


def test_library_home_entry_and_todo_controls(app):
    app.show(Screen.LIBRARY)
    app.handle("prev")                        # Wraps to "Back to menu"
    app.handle("select")
    assert app.current == Screen.MAIN_MENU
    app.todos.add("one")
    app.todos.add("two")
    app.show(Screen.TODO)
    app.handle("next")
    app.handle("toggle")
    assert [t["completed"] for t in app.todos.tasks()] == [False, True]
    app.handle("back")
    assert app.current == Screen.MAIN_MENU


def test_zoom_change_reflows_open_book(app):
    app.show(Screen.LIBRARY)
    app.handle("select")
    pages = app.reader.page_count
    for _ in range(4):
        app.handle("next")
    app.update_settings({"zoom": 1.6})
    assert app.current == Screen.READER and app.reader.page_count > pages and app.reader.current_page > 0


def test_sleep_and_wake(app):
    app.power.sleep_timeout = 0
    app.power.last_activity -= 5
    assert app.power.should_sleep()
    app._sleep()
    assert app.power.is_sleeping
    app.handle("next")                        # Wakes without acting
    assert not app.power.is_sleeping and app.main_menu.current_index == 0


# ---- The web interface ----

@pytest.fixture
def client(app):
    from inkdeck.web.server import WebServer
    return WebServer(app, 5099).flask.test_client()


def test_web_pages_and_books(client, app, tmp_path):
    page = client.get("/")
    assert page.status_code == 200 and b"InkDeck" in page.data and b"Alpha" in page.data
    assert client.get("/static/css/inkdeck.css").status_code == 200
    assert client.get("/static/js/inkdeck.js").status_code == 200

    upload = make_epub(tmp_path / "Gamma Book.epub", "Gamma", chapters=1)
    with open(upload, "rb") as f:
        result = client.post("/upload", data={"file": (f, "Gamma Book.epub")})
    assert result.get_json()["count"] == 1
    assert "Gamma_Book.epub" in [b["filename"] for b in client.get("/api/books").get_json()["books"]]
    assert client.post("/upload", data={"file": (open(upload, "rb"), "notes.txt")}).status_code == 400

    app.progress.save_progress("Gamma_Book.epub", 2, 9, "Chapter 1")
    listed = client.get("/api/progress/list").get_json()["progress"]
    assert listed == [{"path": "Gamma_Book.epub", "filename": "Gamma_Book.epub", "current_page": 3,
                       "total_pages": 9, "percent": 33, "chapter": "Chapter 1",
                       "last_read": listed[0]["last_read"]}]
    assert client.post("/delete/Gamma_Book.epub").get_json()["success"]
    assert client.get("/api/progress/list").get_json()["progress"] == []
    assert client.post("/delete/../config.yaml").status_code == 404
    assert client.post("/delete/Missing.epub").status_code == 404
    assert client.get("/fonts/openSans.ttf").data[:4] == bytes([0, 1, 0, 0])
    assert client.get("/fonts/../config.yaml").status_code == 404
    page = client.get("/").get_data(as_text=True)
    assert 'value="openSans"' in page and 'name="book_fonts"' in page


def test_web_remote_settings_and_todos(client, app):
    assert client.post("/remote/next").get_json()["success"]
    assert app.main_menu.selected_name() == "IP Scanner"
    assert client.post("/remote/open_todo").get_json()["screen"] == "todo"
    assert client.post("/remote/explode").status_code == 400

    saved = client.post("/save_settings", json={"full_refresh_interval": 4, "show_page_numbers": False}).get_json()
    assert saved["status"] == "success" and saved["settings"]["full_refresh_interval"] == 4
    assert app.display.full_refresh_interval == 4 and app.reader.show_page_numbers is False
    assert client.post("/save_settings", data="nope").status_code == 400

    task = client.post("/api/todos", json={"text": "Buy filament"}).get_json()["task"]
    assert client.put(f"/api/todos/{task['id']}").get_json()["task"]["completed"]
    assert client.patch(f"/api/todos/{task['id']}", json={"text": "Buy PETG"}).get_json()["task"]["text"] == "Buy PETG"
    assert client.post("/api/todos", json={"text": "  "}).status_code == 400
    assert client.delete(f"/api/todos/{task['id']}").get_json()["success"]
    assert client.delete("/api/todos/missing").status_code == 404


def test_web_system_endpoints(client):
    stats = client.get("/api/system_stats").get_json()
    assert "ip" in stats and "cpu_temp" in stats
    assert client.get("/api/battery_status").status_code in (200, 503)  # 503 without a battery
    assert client.get("/api/logs/nope").status_code == 404
    assert client.get("/api/ipscanner/status").get_json()["scanning"] is False
    assert client.get("/api/klipper/printers").get_json()["printers"] == []
    assert client.post("/api/bluetooth/pair", json={"mac": "nonsense"}).status_code == 400


def test_wifi_setup_flow(client, app):
    app.wifi.state = "hotspot"
    app._wifi_changed("hotspot")
    assert app.current == Screen.WIFI_SETUP
    assert "InkDeck-Setup" in str(app.main_menu.wifi.setup_active and "InkDeck-Setup")
    for path in ("/", "/generate_204", "/hotspot-detect.html", "/api/books"):
        response = client.get(path)
        assert response.status_code == 302 and response.location.endswith("/setup"), path
    assert b"Connect to Wi-Fi" in client.get("/setup").data
    status = client.get("/api/wifi/status").get_json()
    assert status["setup_active"] and status["state"] == "hotspot"
    assert client.post("/api/wifi/connect", json={"ssid": ""}).status_code == 400
    assert client.post("/api/wifi/connect", json={"ssid": "Home", "password": "short"}).status_code == 400
    joined = []
    app.wifi.connect = lambda *args: joined.append(args)
    assert client.post("/api/wifi/connect", json={"ssid": "Home", "password": "correct horse"}).get_json()["status"] == "connecting"
    assert joined == [("Home", "correct horse", False)]

    app.handle("next")                        # Any button leaves the setup steps
    assert app.current == Screen.MAIN_MENU
    app.show(Screen.LIBRARY)
    app.handle("select")                      # Reading keeps the hotspot's radio on
    assert app.current == Screen.READER and not app._wifi_may_turn_off()
    app.show(Screen.MAIN_MENU)

    app.wifi.state = "normal"
    assert client.get("/").status_code == 200
    assert client.post("/api/wifi/connect", json={"ssid": "Home"}).status_code == 409
    assert client.post("/api/wifi/forget", json={"name": "Nope"}).status_code == 404
