"""Drawing helpers shared by every e-paper screen: fonts, text layout, the
battery indicator and full-screen messages. Screens draw 1-bit images
(0 = black, 1 = white) at the logical portrait size."""

import socket
import subprocess
from functools import lru_cache

from PIL import Image, ImageDraw, ImageFont

BLACK, WHITE = 0, 1

_FONT_DIR = "/usr/share/fonts/truetype/dejavu/"
_FONT_FILES = {
    ("sans", False): "DejaVuSans.ttf",
    ("sans", True): "DejaVuSans-Bold.ttf",
    ("serif", False): "DejaVuSerif.ttf",
    ("serif", True): "DejaVuSerif-Bold.ttf",
    ("mono", False): "DejaVuSansMono.ttf",
    ("mono", True): "DejaVuSansMono-Bold.ttf",
}


@lru_cache(maxsize=None)
def font(size: int, bold: bool = False, family: str = "sans") -> ImageFont.ImageFont:
    """A DejaVu font (installed by fonts-dejavu), or Pillow's default at that size."""
    name = _FONT_FILES[(family, bold)]
    for path in (_FONT_DIR + name, name):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default(size)


def blank(width: int, height: int) -> Image.Image:
    return Image.new("1", (width, height), WHITE)


def text_width(draw: ImageDraw.ImageDraw, text: str, fnt) -> int:
    return int(draw.textlength(text, font=fnt))


def draw_centered(draw: ImageDraw.ImageDraw, y: int, text: str, fnt, width: int):
    draw.text(((width - text_width(draw, text, fnt)) // 2, y), text, font=fnt, fill=BLACK)


def wrap_text(draw: ImageDraw.ImageDraw, text: str, fnt, max_width: int, max_lines: int = 0) -> list:
    """Word-wrap text to max_width pixels. With max_lines, the last line ends in
    an ellipsis when text is cut; words wider than a line are shortened."""
    lines, line = [], ""
    for word in text.split():
        while text_width(draw, word, fnt) > max_width and len(word) > 1:
            word = word[:-2] + "…"
        candidate = f"{line} {word}" if line else word
        if text_width(draw, candidate, fnt) <= max_width:
            line = candidate
        else:
            lines.append(line)
            line = word
    if line:
        lines.append(line)
    if max_lines and len(lines) > max_lines:
        lines = lines[:max_lines]
        last = lines[-1]
        while last and text_width(draw, last + "…", fnt) > max_width:
            last = last[:-1]
        lines[-1] = last.rstrip() + "…"
    return lines or [""]


def draw_battery(draw: ImageDraw.ImageDraw, right: int, top: int, battery):
    """Battery outline, fill, charging bolt and percentage, right-aligned at `right`."""
    if battery is None:
        return
    try:
        percentage = max(0, min(100, int(battery.get_percentage())))
        charging = battery.is_charging()
    except Exception:
        return
    w, h = 30, 14
    x = right - w - 3
    draw.rectangle([x, top, x + w, top + h], outline=BLACK, width=1)
    draw.rectangle([x + w + 1, top + 4, x + w + 3, top + h - 4], fill=BLACK)
    fill = int((w - 4) * percentage / 100)
    if fill > 0:
        draw.rectangle([x + 2, top + 2, x + 2 + fill, top + h - 2], fill=BLACK)
    if charging:
        cx, cy = x + w // 2, top + h // 2
        bolt = [(cx + 2, cy - 6), (cx - 2, cy - 1), (cx + 3, cy - 1),
                (cx - 2, cy + 6), (cx + 2, cy + 1), (cx - 3, cy + 1)]
        draw.polygon(bolt, fill=WHITE, outline=BLACK)
    label = f"{percentage}%"
    small = font(13)
    draw.text((x - text_width(draw, label, small) - 6, top), label, font=small, fill=BLACK)


def draw_title_bar(draw: ImageDraw.ImageDraw, width: int, title: str, battery):
    """Screen title and battery, with a rule underneath. Content starts at y = 80."""
    draw.text((24, 22), title, font=font(26, bold=True), fill=BLACK)
    draw_battery(draw, width - 16, 12, battery)
    draw.line([(24, 62), (width - 24, 62)], fill=BLACK, width=2)


def draw_hint(draw: ImageDraw.ImageDraw, width: int, height: int, hint: str):
    """Control hint centered along the bottom edge."""
    draw.line([(24, height - 44), (width - 24, height - 44)], fill=BLACK, width=1)
    draw_centered(draw, height - 34, hint, font(15), width)


def message_screen(width: int, height: int, message: str) -> Image.Image:
    """A large centered message, used for sleep and shutdown."""
    image = blank(width, height)
    draw = ImageDraw.Draw(image)
    size = 120 if len(message) <= 3 else 96 if len(message) <= 8 else 72 if len(message) <= 15 else 48
    fnt = font(size, bold=True, family="serif")
    lines = wrap_text(draw, message, fnt, width - 60, max_lines=4)
    line_height = int(size * 1.2)
    y = (height - line_height * len(lines)) // 2
    for line in lines:
        draw_centered(draw, y, line, fnt, width)
        y += line_height
    return image


def get_ip_address() -> str:
    """The address other devices use to reach this Pi, or 'No network'."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("10.255.255.255", 1))  # No packet is sent for UDP connect
            return s.getsockname()[0]
    except OSError:
        return "No network"


def wifi_is_up() -> bool:
    try:
        result = subprocess.run(["ip", "-br", "link", "show", "wlan0"],
                                capture_output=True, text=True, timeout=2)
        return result.returncode == 0 and " UP " in f" {result.stdout} "
    except (OSError, subprocess.SubprocessError):
        return False
