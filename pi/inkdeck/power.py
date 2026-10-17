"""Battery-saving controls: CPU cores, Wi-Fi radio and the inactivity timer."""

import logging
import subprocess
import time
from pathlib import Path

CPU_DIR = Path("/sys/devices/system/cpu")


def _run(args, timeout=10) -> bool:
    try:
        return subprocess.run(args, capture_output=True, timeout=timeout).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def count_cpus(listing: str) -> int:
    """Count CPUs in a sysfs list such as '0-3' or '0,2-3'."""
    total = 0
    for part in listing.strip().split(","):
        if "-" in part:
            start, end = part.split("-")
            total += int(end) - int(start) + 1
        elif part:
            total += 1
    return total


class PowerManager:
    def __init__(self, logger=None):
        self.logger = logger or logging.getLogger(__name__)
        self.sleep_enabled = True
        self.sleep_timeout = 120
        self.is_sleeping = False
        self.last_activity = time.monotonic()

    # ---- Inactivity ----

    def reset_activity(self):
        self.last_activity = time.monotonic()

    def should_sleep(self) -> bool:
        return (self.sleep_enabled and not self.is_sleeping
                and time.monotonic() - self.last_activity > self.sleep_timeout)

    # ---- CPU cores ----

    def set_online_cores(self, cores: int):
        """Bring CPUs 1..n-1 online and the rest offline (CPU 0 always stays up).
        Does nothing on kernels without CPU hotplug."""
        try:
            total = count_cpus((CPU_DIR / "present").read_text())
        except (OSError, ValueError):
            return
        if total < 2 or not (CPU_DIR / "cpu1" / "online").exists():
            return
        cores = max(1, min(cores, total))
        for cpu in range(1, total):
            want = "1" if cpu < cores else "0"
            path = CPU_DIR / f"cpu{cpu}" / "online"
            try:
                if path.read_text().strip() == want:
                    continue
            except OSError:
                continue
            if not self._write_root(path, want):
                self.logger.warning(f"Could not set CPU{cpu} online={want}")
        self.logger.info(f"CPU cores online: {self.online_cores()}/{total}")

    def _write_root(self, path: Path, value: str) -> bool:
        try:
            return subprocess.run(["sudo", "-n", "tee", str(path)], input=value.encode(),
                                  capture_output=True, timeout=5).returncode == 0
        except (OSError, subprocess.SubprocessError):
            return False

    @staticmethod
    def online_cores() -> int:
        try:
            return count_cpus((CPU_DIR / "online").read_text())
        except (OSError, ValueError):
            return 1

    # ---- Wi-Fi ----

    def set_wifi(self, on: bool):
        """Switch the Wi-Fi radio. NetworkManager reconnects when it comes back on."""
        state = "on" if on else "off"
        if _run(["sudo", "-n", "nmcli", "radio", "wifi", state]):
            self.logger.info(f"Wi-Fi {state}")
        elif _run(["sudo", "-n", "ip", "link", "set", "wlan0", "up" if on else "down"]):
            self.logger.info(f"Wi-Fi interface {'up' if on else 'down'}")
        else:
            self.logger.warning(f"Could not switch Wi-Fi {state}")
