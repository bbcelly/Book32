"""Klipper printer monitor: finds Moonraker (port 7125) on the local network
and shows each printer's state, temperatures and print progress. Status is
refreshed every three minutes while the screen is open."""

import concurrent.futures
import json
import logging
import threading
import time
import urllib.request
from typing import Dict, List, Optional

from PIL import Image, ImageDraw

from ...ui.common import BLACK, blank, draw_centered, draw_hint, draw_title_bar, font, get_ip_address
from ..ipscanner.screen import port_open, resolve_hostname

MOONRAKER_PORT = 7125


def _get_json(url: str) -> Optional[dict]:
    try:
        with urllib.request.urlopen(url, timeout=3) as response:
            return json.loads(response.read().decode()).get("result")
    except (OSError, ValueError):
        return None


def printer_status(ip: str, hostname: str = "") -> Dict:
    """One printer's state from Moonraker's HTTP API."""
    base = f"http://{ip}:{MOONRAKER_PORT}"
    info = {"ip": ip, "hostname": hostname or ip, "state": "unknown", "klipper_version": None,
            "extruder_temp": None, "extruder_target": None, "bed_temp": None, "bed_target": None,
            "progress": None, "filename": None}
    server = _get_json(f"{base}/server/info") or {}
    info["klipper_version"] = server.get("klippy_state")
    info["state"] = (_get_json(f"{base}/printer/info") or {}).get("state", "unknown")
    status = (_get_json(f"{base}/printer/objects/query?extruder&heater_bed&print_stats&virtual_sdcard")
              or {}).get("status", {})
    if "extruder" in status:
        info["extruder_temp"] = status["extruder"].get("temperature", 0)
        info["extruder_target"] = status["extruder"].get("target", 0)
    if "heater_bed" in status:
        info["bed_temp"] = status["heater_bed"].get("temperature", 0)
        info["bed_target"] = status["heater_bed"].get("target", 0)
    job = status.get("print_stats", {}).get("state", "")
    if job in ("printing", "paused", "complete"):
        info["state"] = job
    elif job == "standby":
        info["state"] = "ready"
    if job in ("printing", "paused"):
        info["filename"] = status.get("print_stats", {}).get("filename")
    if "virtual_sdcard" in status:
        info["progress"] = status["virtual_sdcard"].get("progress", 0)
    return info


class KlipperScreen:
    PER_PAGE = 3
    REFRESH_SECONDS = 180

    def __init__(self, width: int, height: int, battery=None):
        self.logger = logging.getLogger(__name__)
        self.width, self.height = width, height
        self.battery = battery
        self.printers: List[Dict] = []
        self.scanning = False
        self.refreshing = False
        self.scan_progress = 0
        self.current_page = 0
        self.last_refresh = 0.0

    def start_scan(self):
        if self.scanning:
            return
        self.scanning, self.scan_progress, self.printers, self.current_page = True, 0, [], 0
        threading.Thread(target=self._scan, name="klipper-scan", daemon=True).start()

    def _scan(self):
        try:
            local_ip = get_ip_address()
            if local_ip == "No network":
                return
            prefix = local_ip.rsplit(".", 1)[0]
            ips = [f"{prefix}.{i}" for i in range(1, 255)]
            found = []
            with concurrent.futures.ThreadPoolExecutor(max_workers=50) as pool:
                for done, (ip, is_open) in enumerate(
                        zip(ips, pool.map(lambda ip: port_open(ip, MOONRAKER_PORT, 0.5), ips)), 1):
                    self.scan_progress = done * 80 // len(ips)
                    if is_open:
                        found.append(ip)
            printers = []
            for i, ip in enumerate(found):
                printers.append(printer_status(ip, resolve_hostname(ip)))
                self.scan_progress = 80 + (i + 1) * 20 // len(found)
                self.printers = list(printers)
            self.last_refresh = time.monotonic()
            self.logger.info(f"Found {len(printers)} Klipper printers")
        except Exception as e:
            self.logger.error(f"Klipper scan failed: {e}", exc_info=True)
        finally:
            self.scanning, self.scan_progress = False, 100

    def refresh_due(self) -> bool:
        return (bool(self.printers) and not self.scanning and not self.refreshing
                and time.monotonic() - self.last_refresh >= self.REFRESH_SECONDS)

    def refresh_all(self):
        """Poll each known printer again, in the background (no network scan)."""
        if self.refreshing or self.scanning or not self.printers:
            return
        self.refreshing = True

        def work():
            try:
                self.printers = [printer_status(p["ip"], p["hostname"]) for p in self.printers]
            finally:
                self.last_refresh = time.monotonic()
                self.refreshing = False

        threading.Thread(target=work, name="klipper-refresh", daemon=True).start()

    @property
    def page_total(self) -> int:
        return max(1, -(-len(self.printers) // self.PER_PAGE))

    def next_page(self):
        self.current_page = (self.current_page + 1) % self.page_total

    def prev_page(self):
        self.current_page = (self.current_page - 1) % self.page_total

    def render(self) -> Image.Image:
        image = blank(self.width, self.height)
        draw = ImageDraw.Draw(image)
        draw_title_bar(draw, self.width, "Klipper Printers", self.battery)
        body, small, name_font = font(18), font(15), font(20, bold=True)

        if self.scanning:
            draw.text((24, 84), f"Looking for printers... {self.scan_progress}%", font=body, fill=BLACK)
            draw.rectangle([24, 116, self.width - 24, 136], outline=BLACK, width=2)
            fill = (self.width - 52) * self.scan_progress // 100
            if fill > 0:
                draw.rectangle([26, 118, 26 + fill, 134], fill=BLACK)
            hint = "Scanning the network..."
        elif not self.printers:
            draw_centered(draw, self.height // 2 - 20, "No Klipper printers found.", body, self.width)
            draw_centered(draw, self.height // 2 + 12, "Press the button to scan.", small, self.width)
            hint = "Press: scan   |   Hold: menu"
        else:
            y = 84
            start = self.current_page * self.PER_PAGE
            for printer in self.printers[start:start + self.PER_PAGE]:
                draw.rounded_rectangle([16, y, self.width - 16, y + 190], radius=8, outline=BLACK, width=2)
                name = printer["hostname"]
                draw.text((30, y + 12), name if len(name) <= 26 else name[:25] + "…", font=name_font, fill=BLACK)
                state = printer["state"].capitalize()
                draw.text((self.width - 30 - draw.textlength(state, font=body), y + 14), state, font=body, fill=BLACK)
                draw.text((30, y + 44), f"IP {printer['ip']}", font=small, fill=BLACK)
                if printer["extruder_temp"] is not None:
                    draw.text((30, y + 68), f"Nozzle  {printer['extruder_temp']:.0f} / "
                              f"{printer['extruder_target']:.0f} °C", font=body, fill=BLACK)
                if printer["bed_temp"] is not None:
                    draw.text((30, y + 94), f"Bed     {printer['bed_temp']:.0f} / {printer['bed_target']:.0f} °C",
                              font=body, fill=BLACK)
                if printer["state"] in ("printing", "paused") and printer["progress"] is not None:
                    progress = printer["progress"]
                    draw.text((30, y + 124), f"{progress * 100:.0f}%", font=body, fill=BLACK)
                    bar_x, bar_w = 90, self.width - 130
                    draw.rectangle([bar_x, y + 128, bar_x + bar_w, y + 142], outline=BLACK, width=1)
                    if int(bar_w * progress) > 2:
                        draw.rectangle([bar_x + 1, y + 129, bar_x + int(bar_w * progress) - 1, y + 141], fill=BLACK)
                    if printer.get("filename"):
                        file = printer["filename"].split("/")[-1]
                        draw.text((30, y + 154), file if len(file) <= 44 else file[:43] + "…", font=small, fill=BLACK)
                y += 206
            footer = f"Page {self.current_page + 1} of {self.page_total}   ·   " if self.page_total > 1 else ""
            if self.refreshing:
                footer += "Refreshing..."
            elif self.last_refresh:
                minutes = int(time.monotonic() - self.last_refresh) // 60
                footer += "Updated just now" if minutes < 1 else f"Updated {minutes} min ago"
            draw_centered(draw, self.height - 74, footer, small, self.width)
            hint = "Press: next page   |   Hold: menu"
        draw_hint(draw, self.width, self.height, hint)
        return image
