"""The web interface: books, reading positions, to-dos, network tools,
settings, Bluetooth, a terminal and logs, served by Flask on port 5000.

There is no login: anyone on the network can use it, including the terminal,
which runs commands as the app's user. Keep the Pi on a trusted network.
"""

import json
import logging
import os
import platform
import re
import subprocess
import threading
from pathlib import Path

from flask import Flask, Response, jsonify, render_template, request
from werkzeug.serving import make_server
from werkzeug.utils import secure_filename

from .. import VERSION
from ..paths import APP_ROOT, SCRIPTS_DIR, resolve
from ..reader.renderer import forget_layouts
from ..ui.common import get_ip_address, wifi_is_up
from ..ui.navigation import Screen

MAC_RE = re.compile(r"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$")


def _run(args, timeout=5) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.SubprocessError) as e:
        return subprocess.CompletedProcess(args, 1, "", str(e))


def _read(path: str) -> str:
    try:
        return Path(path).read_text().strip()
    except OSError:
        return ""


def system_stats(app) -> dict:
    """Readings for the Info page; missing tools give 'N/A'."""
    def vcgencmd(*args):
        result = _run(["vcgencmd", *args], 2)
        return result.stdout.strip() if result.returncode == 0 else "N/A"

    from ..power import count_cpus
    stats = {
        "cpu_temp": vcgencmd("measure_temp").replace("temp=", ""),
        "cpu_voltage": vcgencmd("measure_volts", "core").replace("volt=", ""),
        "undervolt": app.settings.get("undervolt"),
        "wifi_status": "On" if wifi_is_up() else "Off",
        "bluetooth_status": "On" if _run(["systemctl", "is-active", "bluetooth"], 2).stdout.strip() == "active" else "Off",
        "ip": get_ip_address(),
    }
    freq = _read("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq")
    stats["cpu_speed"] = f"{int(freq) // 1000} MHz" if freq.isdigit() else "N/A"
    throttled = vcgencmd("get_throttled").replace("throttled=", "")
    stats["throttle_status"] = "OK" if throttled == "0x0" else throttled
    try:
        stats["total_cores"] = count_cpus(_read("/sys/devices/system/cpu/present"))
        stats["active_cores"] = count_cpus(_read("/sys/devices/system/cpu/online"))
    except ValueError:
        stats["total_cores"] = stats["active_cores"] = "N/A"
    os_release = dict(line.split("=", 1) for line in _read("/etc/os-release").splitlines() if "=" in line)
    stats["os_name"] = os_release.get("PRETTY_NAME", "").strip('"') or f"{platform.system()} {platform.release()}"
    uptime = _read("/proc/uptime").split()
    if uptime:
        seconds = int(float(uptime[0]))
        days, hours, minutes = seconds // 86400, seconds % 86400 // 3600, seconds % 3600 // 60
        stats["uptime"] = f"{days}d {hours}h {minutes}m" if days else f"{hours}h {minutes}m"
    try:
        disk = os.statvfs("/")
        stats["disk_free"] = f"{disk.f_bavail * disk.f_frsize / 1e9:.1f} GB"
        stats["disk_total"] = f"{disk.f_blocks * disk.f_frsize / 1e9:.1f} GB"
    except (AttributeError, OSError):
        pass
    meminfo = dict(line.split(":", 1) for line in _read("/proc/meminfo").splitlines() if ":" in line)
    try:
        total = int(meminfo["MemTotal"].split()[0]) // 1024
        available = int(meminfo["MemAvailable"].split()[0]) // 1024
        stats["memory_used"], stats["memory_total"] = f"{total - available} MB", f"{total} MB"
        stats["memory_percent"] = round((total - available) * 100 / total)
    except (KeyError, ValueError, ZeroDivisionError):
        pass
    if app.battery:
        stats["battery"] = app.battery.get_status()
    return stats


class WebServer:
    def __init__(self, app, port: int = 5000):
        self.app = app
        self.port = port
        self.logger = logging.getLogger(__name__)
        here = os.path.dirname(__file__)
        self.flask = Flask(__name__, template_folder=os.path.join(here, "templates"),
                           static_folder=os.path.join(here, "static"))
        self.flask.config["MAX_CONTENT_LENGTH"] = 200 * 1024 * 1024
        self._server = None
        self._routes()

    def start(self):
        try:
            self._server = make_server("0.0.0.0", self.port, self.flask, threaded=True)
        except OSError as e:
            self.logger.error(f"Web server could not start on port {self.port}: {e}")
            return
        threading.Thread(target=self._server.serve_forever, name="web", daemon=True).start()

    def stop(self):
        if self._server:
            self._server.shutdown()

    def _book_path(self, filename: str):
        name = secure_filename(filename)
        if not name.lower().endswith(".epub"):
            return None
        return os.path.join(self.app.books_dir, name)

    def _books(self):
        books = []
        for name in sorted(os.listdir(self.app.books_dir), key=str.lower):
            if name.lower().endswith(".epub"):
                size = os.path.getsize(os.path.join(self.app.books_dir, name))
                books.append({"filename": name, "title": name[:-5].replace("_", " "),
                              "size": f"{size / 1048576:.1f} MB"})
        return books

    def _refresh_library(self):
        with self.app.lock:
            self.app.library.load_books(self.app.books_dir)
            if self.app.current == Screen.LIBRARY:
                self.app.render()

    def _routes(self):
        f, app = self.flask, self.app

        @f.route("/")
        def index():
            return render_template("index.html", version=VERSION, settings=app.settings.all(),
                                   books=self._books())

        # ---- Books ----

        @f.route("/api/books")
        def books():
            return jsonify({"books": self._books()})

        @f.route("/upload", methods=["POST"])
        def upload():
            saved = 0
            for file in request.files.getlist("file"):
                path = self._book_path(file.filename or "")
                if path:
                    file.save(path)
                    forget_layouts(path)
                    app.library.covers.forget(path)
                    saved += 1
            if not saved:
                return jsonify({"error": "Choose one or more .epub files"}), 400
            self._refresh_library()
            self.logger.info(f"Uploaded {saved} book(s)")
            return jsonify({"success": True, "count": saved})

        @f.route("/delete/<path:filename>", methods=["POST"])
        def delete(filename):
            path = self._book_path(filename)
            if not path or not os.path.exists(path):
                return jsonify({"error": "Book not found"}), 404
            with app.lock:
                if app.reader.current_book_path == path:
                    app.show(Screen.LIBRARY)
                os.remove(path)
                forget_layouts(path)
                app.library.covers.forget(path)
                app.progress.clear_progress(path)
            self._refresh_library()
            self.logger.info(f"Deleted {os.path.basename(path)}")
            return jsonify({"success": True})

        @f.route("/api/progress/list")
        def list_progress():
            items = []
            for name, entry in app.progress.get_all_progress().items():
                total = max(1, entry.get("total_pages", 1))
                page = entry["current_page"] + 1
                items.append({"path": name, "filename": name, "current_page": page, "total_pages": total,
                              "percent": min(100, round(page * 100 / total)), "chapter": entry.get("chapter", ""),
                              "last_read": entry.get("last_read", "")})
            items.sort(key=lambda item: item["filename"].lower())
            return jsonify({"progress": items})

        @f.route("/api/progress/reset", methods=["POST"])
        def reset_progress():
            target = (request.get_json(silent=True) or {}).get("path")
            if target == "__all__":
                app.progress.clear_all_progress()
                return jsonify({"status": "success", "message": "All reading positions reset"})
            if not target:
                return jsonify({"error": "No book given"}), 400
            app.progress.clear_progress(target)
            return jsonify({"status": "success", "message": f"Reset {os.path.basename(target)}"})

        # ---- Remote control ----

        @f.route("/remote/<command>", methods=["POST"])
        def remote(command):
            if command == "open_todo":
                app.show(Screen.TODO)
            elif command in ("next", "prev", "select", "back", "home", "menu"):
                app.handle(command)
            else:
                return jsonify({"error": f"Unknown command {command}"}), 400
            return jsonify({"success": True, "screen": app.current.value})

        # ---- To-do ----

        @f.route("/api/todos", methods=["GET"])
        def todos():
            return jsonify({"tasks": app.todos.tasks()})

        def todos_changed():
            if app.current == Screen.TODO:
                app.render(partial=True)

        @f.route("/api/todos", methods=["POST"])
        def add_todo():
            text = str((request.get_json(silent=True) or {}).get("text", "")).strip()
            if not text:
                return jsonify({"error": "Task text is required"}), 400
            task = app.todos.add(text[:500])
            todos_changed()
            return jsonify({"success": True, "task": task})

        @f.route("/api/todos/<task_id>", methods=["PUT", "PATCH", "DELETE"])
        def change_todo(task_id):
            if request.method == "DELETE":
                ok = app.todos.delete(task_id)
                task = None
            elif request.method == "PUT":
                task = app.todos.toggle(task_id)
                ok = task is not None
            else:
                text = str((request.get_json(silent=True) or {}).get("text", "")).strip()
                if not text:
                    return jsonify({"error": "Task text is required"}), 400
                task = app.todos.edit(task_id, text[:500])
                ok = task is not None
            if not ok:
                return jsonify({"error": "Task not found"}), 404
            todos_changed()
            return jsonify({"success": True, "task": task})

        # ---- Network tools ----

        @f.route("/api/ipscanner/status")
        def ipscanner_status():
            scanner = app.ip_scanner
            return jsonify({"scanning": scanner.scanning, "progress": scanner.scan_progress,
                            "devices": scanner.devices, "local_ip": get_ip_address()})

        @f.route("/api/ipscanner/scan", methods=["POST"])
        def ipscanner_scan():
            app.ip_scanner.start_scan()
            return jsonify({"status": "started"})

        @f.route("/api/klipper/printers")
        def klipper_printers():
            return jsonify({"printers": app.klipper.printers, "scanning": app.klipper.scanning,
                            "progress": app.klipper.scan_progress})

        @f.route("/api/klipper/scan", methods=["POST"])
        def klipper_scan():
            app.klipper.start_scan()
            return jsonify({"status": "started"})

        # ---- System ----

        @f.route("/api/system_stats")
        def stats():
            return jsonify(system_stats(app))

        @f.route("/api/battery_status")
        def battery_status():
            if not app.battery:
                return jsonify({"error": "No battery monitor"}), 503
            return jsonify(app.battery.get_status())

        @f.route("/save_settings", methods=["POST"])
        def save_settings():
            data = request.get_json(silent=True)
            if not isinstance(data, dict):
                return jsonify({"error": "Expected JSON settings"}), 400
            try:
                warnings = app.update_settings(data)
            except (TypeError, ValueError) as e:
                return jsonify({"error": f"Invalid setting: {e}"}), 400
            return jsonify({"status": "success", "settings": app.settings.all(), **warnings})

        @f.route("/api/reboot", methods=["POST"])
        def reboot():
            self.logger.info("Reboot requested from the web interface")
            app.stop()
            subprocess.Popen(["sudo", "-n", "shutdown", "-r", "now"])
            return jsonify({"status": "rebooting"})

        @f.route("/terminal/execute", methods=["POST"])
        def terminal():
            command = str((request.get_json(silent=True) or {}).get("command", "")).strip()
            if not command:
                return jsonify({"error": "No command"}), 400
            self.logger.info(f"Terminal: {command}")
            if command in ("git pull", "update"):
                command = f"bash {SCRIPTS_DIR / 'update.sh'}"

            def stream():
                try:
                    process = subprocess.Popen(command, shell=True, cwd=APP_ROOT, text=True, bufsize=1,
                                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                    for line in process.stdout:
                        yield f"data: {json.dumps({'stdout': line})}\n\n"
                    yield f"data: {json.dumps({'returncode': process.wait()})}\n\n"
                except OSError as e:
                    yield f"data: {json.dumps({'error': str(e)})}\n\n"

            return Response(stream(), mimetype="text/event-stream")

        @f.route("/api/logs/<kind>")
        def logs(kind):
            if kind == "app":
                path = resolve(app.config.get("logging.file", "logs/inkdeck.log"))
                result = _run(["tail", "-n", "300", str(path)])
            elif kind == "system":
                result = _run(["journalctl", "-u", "inkdeck", "-n", "300", "--no-pager"], 10)
            else:
                return jsonify({"error": "Unknown log"}), 404
            return jsonify({"logs": result.stdout or result.stderr or "No log entries.", "type": kind})

        # ---- Bluetooth ----

        helper = str(SCRIPTS_DIR / "bluetooth_helper.sh")

        def bluetooth(*args, timeout=10):
            return _run(["sudo", "-n", helper, *args], timeout)

        def device_lines(output: str):
            for line in output.splitlines():
                parts = line.split(" ", 2)
                if len(parts) >= 2 and parts[0] == "Device" and MAC_RE.match(parts[1]):
                    yield parts[1], parts[2] if len(parts) > 2 else parts[1]

        @f.route("/api/bluetooth/status")
        def bluetooth_status():
            available = _run(["systemctl", "is-active", "bluetooth"], 2).stdout.strip() == "active"
            powered = available and "Powered: yes" in _run(["bluetoothctl", "show"], 5).stdout
            paired = [{"mac": m, "name": n} for m, n in device_lines(bluetooth("paired_devices").stdout)] \
                if powered else []
            return jsonify({"available": available, "powered": powered, "paired_devices": paired})

        @f.route("/api/bluetooth/power", methods=["POST"])
        def bluetooth_power():
            on = bool((request.get_json(silent=True) or {}).get("power"))
            result = bluetooth("power_on" if on else "power_off")
            if result.returncode != 0:
                return jsonify({"error": result.stderr or result.stdout or "Bluetooth is not available"}), 500
            return jsonify({"success": True, "powered": on})

        @f.route("/api/bluetooth/scan", methods=["POST"])
        def bluetooth_scan():
            on = bool((request.get_json(silent=True) or {}).get("scan"))
            bluetooth("scan_on" if on else "scan_off")
            return jsonify({"success": True, "scanning": on})

        @f.route("/api/bluetooth/devices")
        def bluetooth_devices():
            paired = {mac for mac, _ in device_lines(bluetooth("paired_devices").stdout)}
            devices = [{"mac": m, "name": n, "paired": m in paired}
                       for m, n in dict(device_lines(bluetooth("devices").stdout)).items()]
            return jsonify({"devices": devices})

        @f.route("/api/bluetooth/pair", methods=["POST"])
        def bluetooth_pair():
            data = request.get_json(silent=True) or {}
            mac, pin = str(data.get("mac", "")), str(data.get("pin", ""))
            if not MAC_RE.match(mac) or (pin and not pin.isdigit()):
                return jsonify({"error": "Invalid device or PIN"}), 400
            return jsonify(_pair(helper, mac, pin, self.logger))

        @f.route("/api/bluetooth/remove", methods=["POST"])
        def bluetooth_remove():
            mac = str((request.get_json(silent=True) or {}).get("mac", ""))
            if not MAC_RE.match(mac):
                return jsonify({"error": "Invalid device"}), 400
            result = bluetooth("remove", mac)
            if result.returncode != 0:
                return jsonify({"error": result.stderr or "Could not remove the device"}), 500
            return jsonify({"success": True})


def _pair(helper: str, mac: str, pin: str, logger) -> dict:
    """Run the pairing helper. A keyboard shows a passkey to type: that is
    returned as soon as it appears while pairing finishes in the background."""
    import time
    process = subprocess.Popen(["sudo", "-n", helper, "pair", mac, pin], text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = []
    deadline = time.monotonic() + (60 if pin else 20)
    while time.monotonic() < deadline:
        line = process.stdout.readline()
        if not line:
            if process.poll() is not None:
                break
            continue
        output.append(line)
        match = re.search(r"PASSKEY_REQUIRED:(\d+)", line)
        if match and not pin:
            threading.Thread(target=process.communicate, daemon=True).start()
            return {"success": True, "status": "passkey_required", "passkey": match.group(1),
                    "message": f"Type {match.group(1)} on the keyboard, then press Enter"}
        if "PAIRING_SUCCESS" in line:
            threading.Thread(target=process.communicate, daemon=True).start()
            return {"success": True, "status": "paired"}
    text = "".join(output)
    logger.info(f"Bluetooth pairing output: {text[-500:]}")
    if process.poll() is None:
        threading.Thread(target=process.communicate, daemon=True).start()
    if "DEVICE_NOT_AVAILABLE" in text or "not available" in text.lower():
        error = "Device not found. Scan again and pair while it is in pairing mode."
    elif "ConnectionAttemptFailed" in text:
        error = "Could not connect. Wake the device, then try again straight away."
    elif "PAIRING_TIMEOUT" in text:
        error = "Pairing timed out. Wake the device and try again."
    else:
        error = "Pairing failed. Put the device in pairing mode, or try a PIN such as 0000."
    return {"success": False, "error": error}
