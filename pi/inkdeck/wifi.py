"""Wi-Fi setup, as on InkDeck for ESP32.

When the Pi can't join a saved network (a new owner, a new router, a move),
it opens its own open Wi-Fi network, InkDeck-Setup, and the e-paper shows
how to join it. Joining from a phone opens the setup page at 192.168.4.1
(the phone shows it as a sign-in page): pick a network, type its password,
and the Pi saves it and joins it. If that fails, InkDeck-Setup comes back.

While the hotspot is up and nobody is using the setup page, the Pi tries its
saved networks again every few minutes, so a router that was off for a while
is picked up again by itself.

Everything goes through NetworkManager (nmcli), which Raspberry Pi OS uses.
"""

import logging
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Callable, List, Optional

SETUP_SSID = "InkDeck-Setup"
SETUP_ADDRESS = "192.168.4.1"
HOTSPOT = "inkdeck-setup"           # NetworkManager connection name of the hotspot
INTERFACE = "wlan0"
OFFLINE_WAIT = 90                   # Seconds without a network before the hotspot starts
FIRST_WAIT = 20                     # The same, when no network is saved at all
RETRY_EVERY = 300                   # Hotspot up this long without visitors: try saved networks
RETRY_WAIT = 45                     # How long a retry waits for a saved network
# Every name looked up on the hotspot answers with the Pi's address, so phones
# find the setup page (NetworkManager's dnsmasq reads this folder for hotspots).
DNS_CONF = Path("/etc/NetworkManager/dnsmasq-shared.d/inkdeck-setup.conf")
DNS_RULE = f"address=/#/{SETUP_ADDRESS}\n"


def split_terse(line: str) -> List[str]:
    """Fields of an `nmcli -t -e yes` line: ':' separates, backslash escapes."""
    fields, current, escaped = [], "", False
    for ch in line:
        if escaped:
            current += ch
            escaped = False
        elif ch == "\\":
            escaped = True
        elif ch == ":":
            fields.append(current)
            current = ""
        else:
            current += ch
    fields.append(current)
    return fields


def parse_scan(output: str) -> List[dict]:
    """Networks from `nmcli -t -e yes -f SSID,SIGNAL,SECURITY dev wifi list`:
    one entry per name, strongest first; hidden networks and our own hotspot left out."""
    best = {}
    for line in output.splitlines():
        fields = split_terse(line)
        if len(fields) < 3 or not fields[0] or fields[0] == SETUP_SSID:
            continue
        ssid, security = fields[0], fields[2].strip()
        signal = int(fields[1]) if fields[1].isdigit() else 0
        if ssid not in best or signal > best[ssid]["signal"]:
            best[ssid] = {"ssid": ssid, "signal": signal, "secure": security not in ("", "--")}
    return sorted(best.values(), key=lambda n: -n["signal"])


def password_error(password: str) -> Optional[str]:
    if password and not 8 <= len(password) <= 63:
        return "Wi-Fi passwords are 8 to 63 characters"
    return None


class WifiManager:
    """Watches the connection and runs the setup hotspot.

    `on_change(event)` is called with "hotspot", "connecting", "connected" or
    "failed"; `radio_wanted()` says whether Wi-Fi should be on now (it is off
    while reading and asleep, and then being offline is expected)."""

    def __init__(self, on_change: Callable[[str], None] = lambda event: None,
                 radio_wanted: Callable[[], bool] = lambda: True):
        self.logger = logging.getLogger(__name__)
        self.on_change = on_change
        self.radio_wanted = radio_wanted
        self.available = sys.platform.startswith("linux") and shutil.which("nmcli") is not None
        self.state = "normal"               # normal, hotspot, connecting, retrying
        self.ssid = ""                      # The network being joined, or just joined
        self.message = ""                   # Result of the last attempt, for the setup page
        self.networks: List[dict] = []      # Last scan, made before the hotspot started
        self.last_visit = 0.0               # Last request to the setup page
        self.hotspot_since = 0.0
        self._lock = threading.Lock()       # One hotspot or connect step at a time

    @property
    def setup_active(self) -> bool:
        """The hotspot is up, or the Pi is busy leaving it."""
        return self.state != "normal"

    # ---- nmcli ----

    def _nmcli(self, *args, timeout=20) -> subprocess.CompletedProcess:
        try:
            return subprocess.run(["sudo", "-n", "nmcli", *args], capture_output=True, text=True, timeout=timeout)
        except (OSError, subprocess.SubprocessError) as e:
            return subprocess.CompletedProcess(args, 1, "", str(e))

    def connection(self) -> Optional[str]:
        """The saved network wlan0 is connected to, or None."""
        if not self.available:
            return None
        result = self._nmcli("-t", "-e", "yes", "-f", "DEVICE,STATE,CONNECTION", "device", timeout=5)
        for line in result.stdout.splitlines():
            fields = split_terse(line)
            if len(fields) >= 3 and fields[0] == INTERFACE and fields[1] == "connected" and fields[2] != HOTSPOT:
                return fields[2]
        return None

    def connected_ssid(self) -> Optional[str]:
        name = self.connection()
        if not name:
            return None
        return self._nmcli("-g", "802-11-wireless.ssid", "connection", "show", name, timeout=5).stdout.strip() or name

    def saved(self) -> List[dict]:
        """Saved Wi-Fi networks: [{"name", "ssid"}]."""
        if not self.available:
            return []
        result = self._nmcli("-t", "-e", "yes", "-f", "NAME,TYPE", "connection", "show", timeout=5)
        networks = []
        for line in result.stdout.splitlines():
            fields = split_terse(line)
            if len(fields) >= 2 and fields[1] == "802-11-wireless" and fields[0] != HOTSPOT:
                ssid = self._nmcli("-g", "802-11-wireless.ssid", "connection", "show", fields[0], timeout=5)
                networks.append({"name": fields[0], "ssid": ssid.stdout.strip() or fields[0]})
        return networks

    def scan(self, rescan: bool = True) -> List[dict]:
        """Nearby networks. While the hotspot is up the radio can't always scan,
        so the list made before it started is kept and added to."""
        if not self.available:
            return self.networks
        result = self._nmcli("-t", "-e", "yes", "-f", "SSID,SIGNAL,SECURITY", "device", "wifi", "list",
                             "ifname", INTERFACE, "--rescan", "yes" if rescan else "no", timeout=30)
        found = parse_scan(result.stdout)
        if self.state != "normal":
            known = {n["ssid"] for n in found}
            found += [n for n in self.networks if n["ssid"] not in known]
        self.networks = found
        return found

    # ---- The hotspot ----

    def start_hotspot(self) -> bool:
        if not self.available:
            return False
        with self._lock:
            self._nmcli("radio", "wifi", "on", timeout=10)
            self.state = "normal"   # So the scan below replaces the list
            self.scan()
            self._write_dns_rule()
            self._nmcli("connection", "delete", HOTSPOT, timeout=10)
            added = self._nmcli("connection", "add", "type", "wifi", "ifname", INTERFACE, "con-name", HOTSPOT,
                                "autoconnect", "no", "ssid", SETUP_SSID,
                                "802-11-wireless.mode", "ap", "802-11-wireless.band", "bg",
                                "ipv4.method", "shared", "ipv4.addresses", f"{SETUP_ADDRESS}/24",
                                "ipv6.method", "disabled")
            up = self._nmcli("connection", "up", HOTSPOT, timeout=40) if added.returncode == 0 else added
            if up.returncode != 0:
                self.logger.error(f"Setup hotspot failed: {(up.stderr or up.stdout).strip()}")
                return False
            self.state = "hotspot"
            self.hotspot_since = time.monotonic()
        self.logger.info(f"Setup hotspot {SETUP_SSID} is up at {SETUP_ADDRESS}")
        self.on_change("hotspot")
        return True

    def _write_dns_rule(self):
        try:
            if DNS_CONF.read_text() == DNS_RULE:
                return
        except OSError:
            pass
        subprocess.run(["sudo", "-n", "mkdir", "-p", str(DNS_CONF.parent)], capture_output=True)
        subprocess.run(["sudo", "-n", "tee", str(DNS_CONF)], input=DNS_RULE, text=True, capture_output=True)

    def stop_hotspot(self):
        self._nmcli("connection", "down", HOTSPOT, timeout=20)

    def visited(self):
        """The setup page was used: don't leave the hotspot to retry for a while."""
        self.last_visit = time.monotonic()

    # ---- Joining a network ----

    def connect(self, ssid: str, password: str = "", hidden: bool = False):
        """From the setup page: leave the hotspot and join `ssid` (in the
        background, so the page gets its answer first). Saved on success;
        on failure the hotspot comes back with the reason."""
        self.state, self.ssid, self.message = "connecting", ssid, ""
        threading.Thread(target=self._connect, args=(ssid, password, hidden), name="wifi-connect", daemon=True).start()

    def _connect(self, ssid, password, hidden):
        time.sleep(2)  # Let the page receive its answer before the hotspot goes
        self.on_change("connecting")
        with self._lock:
            self.stop_hotspot()
            for network in self.saved():  # Typing the password again replaces the old one
                if network["ssid"] == ssid:
                    self._nmcli("connection", "delete", network["name"], timeout=10)
            args = ["--wait", "45", "device", "wifi", "connect", ssid]
            if password:
                args += ["password", password]
            args += ["ifname", INTERFACE] + (["hidden", "yes"] if hidden else [])
            result = None
            for attempt in range(3):
                result = self._nmcli(*args, timeout=60)
                if result.returncode == 0 or "No network with SSID" not in result.stderr + result.stdout:
                    break
                self._nmcli("device", "wifi", "rescan", "ifname", INTERFACE, timeout=10)
                time.sleep(5)  # Not seen yet: the radio was busy with the hotspot
        if result.returncode == 0:
            self.state = "normal"
            self.message = f"Connected to {ssid}"
            self.logger.info(f"Joined {ssid}")
            self.on_change("connected")
            return
        error = (result.stderr or result.stdout).strip()
        self.logger.warning(f"Could not join {ssid}: {error}")
        for network in self.saved():  # Don't keep a network that didn't work
            if network["ssid"] == ssid:
                self._nmcli("connection", "delete", network["name"], timeout=10)
        lowered = error.lower()
        if "secrets were required" in lowered or "802-1x" in lowered or "password" in lowered:
            self.message = f"Could not join {ssid}: check the password and try again."
        elif "no network with ssid" in lowered:
            self.message = f"Could not find {ssid}. Check the name, and that it's in range."
        else:
            self.message = f"Could not join {ssid}. Check the password and try again."
        self.on_change("failed")
        if not self.start_hotspot():
            self.state = "normal"

    def add(self, ssid: str, password: str = "", hidden: bool = False) -> subprocess.CompletedProcess:
        """From the normal web interface: save a network for later without
        leaving the current one. NetworkManager joins it when it's in range."""
        args = ["connection", "add", "type", "wifi", "ifname", INTERFACE, "con-name", ssid, "ssid", ssid,
                "connection.autoconnect", "yes"]
        if password:
            args += ["wifi-sec.key-mgmt", "wpa-psk", "wifi-sec.psk", password]
        if hidden:
            args += ["802-11-wireless.hidden", "yes"]
        for network in self.saved():
            if network["ssid"] == ssid:
                self._nmcli("connection", "delete", network["name"], timeout=10)
        return self._nmcli(*args)

    def forget(self, name: str) -> bool:
        if name == HOTSPOT or name not in {n["name"] for n in self.saved()}:
            return False
        return self._nmcli("connection", "delete", name, timeout=10).returncode == 0

    # ---- Watching ----

    def start(self):
        if self.available:
            threading.Thread(target=self._watch, name="wifi", daemon=True).start()

    def _watch(self):
        offline_since = time.monotonic()
        while True:
            time.sleep(5)
            try:
                now = time.monotonic()
                if self.state == "hotspot":
                    idle = now - max(self.last_visit, self.hotspot_since)
                    if idle > RETRY_EVERY and self.saved():
                        self._retry_saved()
                    continue
                if self.state != "normal":
                    continue
                if not self.radio_wanted() or self.connection():
                    offline_since = now
                    continue
                wait = OFFLINE_WAIT if self.saved() else FIRST_WAIT
                if now - offline_since > wait:
                    self.logger.info(f"No Wi-Fi for {wait} s: starting the setup hotspot")
                    if not self.start_hotspot():
                        offline_since = now  # Try again after another wait
            except Exception as e:
                self.logger.error(f"Wi-Fi watcher: {e}", exc_info=True)

    def _retry_saved(self):
        """Leave the hotspot for a while so NetworkManager can join a saved network."""
        self.logger.info("Setup hotspot unused: trying the saved networks again")
        with self._lock:
            self.state = "retrying"
            self.stop_hotspot()
        deadline = time.monotonic() + RETRY_WAIT
        while time.monotonic() < deadline:
            time.sleep(3)
            if self.connection():
                self.state = "normal"
                self.message = ""
                self.on_change("connected")
                return
        if not self.start_hotspot():
            self.state = "normal"
