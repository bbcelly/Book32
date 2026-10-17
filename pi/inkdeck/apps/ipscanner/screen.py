"""Network scanner: lists devices on the local /24 network.

Uses arp-scan when installed (fastest, shows vendors), then nmap, then a
ping sweep. Host names come from mDNS (avahi) or reverse DNS.
"""

import concurrent.futures
import logging
import socket
import subprocess
import threading
from typing import Dict, List

from PIL import Image, ImageDraw

from ...ui.common import BLACK, blank, draw_centered, draw_hint, draw_title_bar, font, get_ip_address


def _run(args, timeout):
    try:
        return subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.SubprocessError):
        return None


def resolve_hostname(ip: str) -> str:
    result = _run(["avahi-resolve", "-a", ip], 5)
    if result and result.returncode == 0 and "\t" in result.stdout:
        return result.stdout.strip().split("\t")[1].strip()
    result = _run(["getent", "hosts", ip], 5)
    if result and result.returncode == 0:
        parts = result.stdout.split()
        if len(parts) >= 2 and parts[1] != ip:
            return parts[1]
    try:
        return socket.gethostbyaddr(ip)[0]
    except OSError:
        return ""


def port_open(ip: str, port: int, timeout: float = 1.0) -> bool:
    try:
        with socket.create_connection((ip, port), timeout=timeout):
            return True
    except OSError:
        return False


class IPScannerScreen:
    PER_PAGE = 26

    def __init__(self, width: int, height: int, battery=None):
        self.logger = logging.getLogger(__name__)
        self.width, self.height = width, height
        self.battery = battery
        self.devices: List[Dict] = []
        self.scanning = False
        self.scan_progress = 0
        self.current_page = 0

    def start_scan(self):
        if self.scanning:
            return
        self.scanning, self.scan_progress, self.devices, self.current_page = True, 0, [], 0
        threading.Thread(target=self._scan, name="ip-scan", daemon=True).start()

    def _scan(self):
        try:
            local_ip = get_ip_address()
            if local_ip == "No network":
                return
            prefix = local_ip.rsplit(".", 1)[0]
            self.scan_progress = 10
            found = self._arp_scan() or self._nmap(prefix) or self._ping_sweep(prefix)
            self.scan_progress = 90
            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                for device, (hostname, http) in zip(found, pool.map(
                        lambda d: (resolve_hostname(d["ip"]), port_open(d["ip"], 80)), found)):
                    device["hostname"], device["http"] = hostname, http
            found.sort(key=lambda d: tuple(int(p) for p in d["ip"].split(".")))
            self.devices = found
            self.logger.info(f"Network scan found {len(found)} devices")
        except Exception as e:
            self.logger.error(f"Network scan failed: {e}", exc_info=True)
        finally:
            self.scanning, self.scan_progress = False, 100

    def _arp_scan(self) -> List[Dict]:
        result = _run(["sudo", "-n", "arp-scan", "--localnet", "--interface=wlan0"], 30)
        if not result or result.returncode != 0:
            return []
        devices = []
        for line in result.stdout.splitlines():
            parts = line.split("\t")
            if len(parts) >= 2 and parts[0].count(".") == 3:
                devices.append({"ip": parts[0].strip(), "mac": parts[1].strip(),
                                "name": parts[2].strip() if len(parts) > 2 else "Unknown"})
        return devices

    def _nmap(self, prefix: str) -> List[Dict]:
        result = _run(["nmap", "-sn", f"{prefix}.0/24"], 60)
        if not result or result.returncode != 0:
            return []
        devices, ip = [], None
        for line in result.stdout.splitlines():
            if line.startswith("Nmap scan report for"):
                ip = line.split()[-1].strip("()")
                devices.append({"ip": ip, "mac": "Unknown", "name": "Unknown"})
            elif "MAC Address:" in line and devices:
                mac, _, vendor = line.split("MAC Address:")[1].strip().partition(" ")
                devices[-1].update(mac=mac, name=vendor.strip("()") or "Unknown")
        return devices

    def _ping_sweep(self, prefix: str) -> List[Dict]:
        def ping(i):
            ip = f"{prefix}.{i}"
            result = _run(["ping", "-c", "1", "-W", "1", ip], 3)
            return ip if result and result.returncode == 0 else None

        devices = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=50) as pool:
            for done, ip in enumerate(pool.map(ping, range(1, 255)), 1):
                self.scan_progress = 10 + done * 80 // 254
                if ip:
                    devices.append({"ip": ip, "mac": "Unknown", "name": "Unknown"})
        return devices

    @property
    def page_total(self) -> int:
        return max(1, -(-len(self.devices) // self.PER_PAGE))

    def next_page(self):
        self.current_page = (self.current_page + 1) % self.page_total

    def prev_page(self):
        self.current_page = (self.current_page - 1) % self.page_total

    def render(self) -> Image.Image:
        image = blank(self.width, self.height)
        draw = ImageDraw.Draw(image)
        draw_title_bar(draw, self.width, "Network Scanner", self.battery)
        body, small = font(18), font(15)
        draw.text((24, 74), f"This device: {get_ip_address()}", font=body, fill=BLACK)

        if self.scanning:
            draw.text((24, 120), f"Scanning... {self.scan_progress}%", font=body, fill=BLACK)
            draw.rectangle([24, 152, self.width - 24, 172], outline=BLACK, width=2)
            fill = (self.width - 52) * self.scan_progress // 100
            if fill > 0:
                draw.rectangle([26, 154, 26 + fill, 170], fill=BLACK)
            hint = "Scanning the network..."
        elif not self.devices:
            draw_centered(draw, self.height // 2 - 20, "No devices listed yet.", body, self.width)
            draw_centered(draw, self.height // 2 + 12, "Press the button to scan.", small, self.width)
            hint = "Press: scan   |   Hold: menu"
        else:
            draw.text((24, 104), f"Found {len(self.devices)} devices", font=body, fill=BLACK)
            y = 136
            start = self.current_page * self.PER_PAGE
            for device in self.devices[start:start + self.PER_PAGE]:
                label = device.get("hostname") or (device["name"] if device["name"] != "Unknown" else "")
                draw.text((24, y), device["ip"], font=small, fill=BLACK)
                draw.text((150, y), (label[:36] + "…") if len(label) > 37 else label, font=small, fill=BLACK)
                y += 22
            if self.page_total > 1:
                draw_centered(draw, self.height - 72, f"Page {self.current_page + 1} of {self.page_total}",
                              small, self.width)
            hint = "Press: next page   |   Hold: menu"
        draw_hint(draw, self.width, self.height, hint)
        return image
