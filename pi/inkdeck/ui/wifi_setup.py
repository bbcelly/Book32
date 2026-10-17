"""The Wi-Fi setup screen, shown while the setup hotspot is up (see wifi.py)."""

from typing import Optional

from PIL import Image, ImageDraw

from ..wifi import SETUP_ADDRESS, SETUP_SSID
from .common import BLACK, WHITE, blank, draw_centered, draw_hint, draw_title_bar, font, get_ip_address, wrap_text


def qr_image(data: str, size: int) -> Optional[Image.Image]:
    """A QR code about `size` pixels square, or None without the qrcode package."""
    try:
        import qrcode
    except ImportError:
        return None
    code = qrcode.QRCode(border=2, error_correction=qrcode.constants.ERROR_CORRECT_M)
    code.add_data(data)
    code.make(fit=True)
    matrix = code.get_matrix()
    cell = max(1, size // len(matrix))
    image = blank(len(matrix) * cell, len(matrix) * cell)
    draw = ImageDraw.Draw(image)
    for y, row in enumerate(matrix):
        for x, dark in enumerate(row):
            if dark:
                draw.rectangle([x * cell, y * cell, x * cell + cell - 1, y * cell + cell - 1], fill=BLACK)
    return image


class WifiSetupScreen:
    """Steps for joining InkDeck-Setup, then the result of joining a network."""

    def __init__(self, width: int, height: int, wifi, battery=None, web_port: int = 5000):
        self.width, self.height = width, height
        self.wifi = wifi
        self.battery = battery
        self.web_port = web_port

    def _paragraph(self, draw, y, text, fnt, indent=0) -> int:
        for line in wrap_text(draw, text, fnt, self.width - 48 - indent):
            draw.text((24 + indent, y), line, font=fnt, fill=BLACK)
            y += int(fnt.size * 1.3)
        return y

    def _step(self, image, draw, y, number, text, bold, qr_data) -> int:
        body, strong = font(19), font(24, bold=True)
        draw.ellipse([24, y, 56, y + 32], fill=BLACK)
        draw_centered_in(draw, 40, y + 4, str(number), font(20, bold=True))
        y = self._paragraph(draw, y + 4, text, body, indent=46)
        if bold:
            draw.text((70, y + 2), bold, font=strong, fill=BLACK)
            y += 36
        code = qr_image(qr_data, 150) if qr_data else None
        if code:
            image.paste(code, ((self.width - code.width) // 2, y + 6))
            y += code.height + 12
        return y + 14

    def render(self) -> Image.Image:
        image = blank(self.width, self.height)
        draw = ImageDraw.Draw(image)
        draw_title_bar(draw, self.width, "Wi-Fi setup", self.battery)
        state, ssid = self.wifi.state, self.wifi.ssid
        y = 86
        if state in ("connecting", "retrying"):
            text = f"Joining {ssid}…" if state == "connecting" else "Trying the saved Wi-Fi networks…"
            y = self._paragraph(draw, 300, text, font(26, bold=True))
            self._paragraph(draw, y + 16, f"If it doesn't work, {SETUP_SSID} comes back in about a minute.", font(19))
        elif state == "normal":
            address = get_ip_address()
            y = self._paragraph(draw, 150, f"Connected to {ssid}" if ssid else "Connected to Wi-Fi", font(28, bold=True))
            y = self._paragraph(draw, y + 20, "Open the web interface to add books and change settings:", font(19))
            url = f"http://{address}:{self.web_port}"
            draw_centered(draw, y + 10, url, font(22, bold=True), self.width)
            code = qr_image(url, 180)
            if code:
                image.paste(code, ((self.width - code.width) // 2, y + 56))
        else:
            if self.wifi.message:
                y = self._paragraph(draw, y, self.wifi.message, font(19, bold=True)) + 12
            else:
                y = self._paragraph(draw, y, "Connect InkDeck to your Wi-Fi using a phone or computer.", font(19)) + 12
            y = self._step(image, draw, y, 1, "Join this Wi-Fi network (or scan the code with your phone's camera):",
                           SETUP_SSID, f"WIFI:S:{SETUP_SSID};T:nopass;;")
            y = self._step(image, draw, y, 2, "The setup page opens by itself. If it doesn't, open:",
                           f"http://{SETUP_ADDRESS}", None)
            self._step(image, draw, y, 3, "Choose your Wi-Fi network and enter its password.", None, None)
        draw_hint(draw, self.width, self.height, "Press: main menu")
        return image


def draw_centered_in(draw, cx, y, text, fnt):
    draw.text((cx - draw.textlength(text, font=fnt) / 2, y), text, font=fnt, fill=WHITE)
