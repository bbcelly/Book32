"""Commands from the PiSugar 2 custom button, received on a Unix socket.

In the PiSugar web UI, set a tap action to a shell command such as
    echo next | nc -U -q1 /tmp/inkdeck-button.sock
Each connection carries one command word (see InkDeckApp.handle).
"""

import logging
import os
import socket
import threading
from typing import Callable


class PiSugarButtonServer:
    def __init__(self, socket_path: str, on_command: Callable[[str], None]):
        self.logger = logging.getLogger(__name__)
        self.socket_path = socket_path
        self.on_command = on_command
        self.running = False
        self._server = None

    def start(self):
        try:
            if os.path.exists(self.socket_path):
                os.unlink(self.socket_path)
            self._server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self._server.bind(self.socket_path)
            self._server.listen(5)
            self._server.settimeout(1.0)
        except OSError as e:
            self.logger.error(f"PiSugar button socket {self.socket_path}: {e}")
            return
        self.running = True
        threading.Thread(target=self._serve, name="pisugar-button", daemon=True).start()
        self.logger.info(f"PiSugar button commands on {self.socket_path}")

    def _serve(self):
        while self.running:
            try:
                conn, _ = self._server.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            with conn:
                conn.settimeout(1.0)
                try:
                    command = conn.recv(256).decode(errors="ignore").strip()
                except OSError:
                    continue
            if command:
                self.logger.info(f"PiSugar button: {command}")
                try:
                    self.on_command(command)
                except Exception as e:
                    self.logger.error(f"PiSugar button command '{command}': {e}", exc_info=True)

    def stop(self):
        self.running = False
        if self._server:
            self._server.close()
        try:
            os.unlink(self.socket_path)
        except OSError:
            pass
