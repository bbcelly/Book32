"""The main menu, the book library and the reader."""

import logging
import os
from functools import lru_cache
from datetime import datetime
from typing import Dict, List, Optional

from PIL import Image, ImageDraw

from .. import VERSION
from ..paths import ASSETS_DIR
from ..reader.covers import CoverExtractor
from ..reader.page_cache import PageCache
from .common import (BLACK, WHITE, blank, draw_battery, draw_centered, draw_hint,
                     draw_title_bar, font, get_ip_address, text_width, wrap_text)
from .navigation import Screen

HOME_ENTRY = "__home__"


class MainMenuScreen:
    """App grid. Press moves to the next app; hold opens it."""

    APPS = [
        ("eReader", "ereader.png", Screen.LIBRARY),
        ("IP Scanner", "ip_scanner.png", Screen.IP_SCANNER),
        ("To Do", "todo.png", Screen.TODO),
        ("Klipper", "klipper.png", Screen.KLIPPER),
        ("Terminal", "terminal.png", Screen.TYPEWRITER),
        ("Shut Down", "shutdown.png", "shutdown"),
    ]
    ICON = 120

    def __init__(self, width: int, height: int, battery=None, web_port: int = 5000):
        self.width, self.height = width, height
        self.battery = battery
        self.web_port = web_port
        self.current_index = 0
        self.icons = {}
        for name, filename, _ in self.APPS:
            try:
                icon = Image.open(ASSETS_DIR / "icons" / filename).convert("L")
                icon = icon.resize((self.ICON, self.ICON), Image.Resampling.LANCZOS)
                self.icons[name] = icon.point(lambda v: 255 if v >= 128 else 0, "1")
            except OSError:
                logging.getLogger(__name__).warning(f"Icon missing: {filename}")

    def next_app(self):
        self.current_index = (self.current_index + 1) % len(self.APPS)

    def prev_app(self):
        self.current_index = (self.current_index - 1) % len(self.APPS)

    def selected_target(self):
        return self.APPS[self.current_index][2]

    def selected_name(self) -> str:
        return self.APPS[self.current_index][0]

    def render(self) -> Image.Image:
        image = blank(self.width, self.height)
        draw = ImageDraw.Draw(image)
        now = self.battery.get_time() if self.battery else datetime.now()
        small = font(15)
        draw.text((14, 10), now.strftime("%I:%M %p").lstrip("0"), font=small, fill=BLACK)
        draw.text((14, 30), now.strftime("%b %d, %Y").replace(" 0", " "), font=small, fill=BLACK)
        draw_battery(draw, self.width - 14, 12, self.battery)

        title, version = font(40, bold=True), font(13, bold=True)
        title_w = text_width(draw, "InkDeck", title)
        x = (self.width - title_w - text_width(draw, f" v{VERSION}", version)) // 2
        draw.text((x, 46), "InkDeck", font=title, fill=BLACK)
        draw.text((x + title_w + 6, 70), f"v{VERSION}", font=version, fill=BLACK)

        label = font(20)
        for index, (name, _, _) in enumerate(self.APPS):
            cx = self.width // 4 + (index % 2) * (self.width // 2)
            y = 128 + (index // 2) * 210
            if index == self.current_index:
                draw.rounded_rectangle([cx - 80, y - 10, cx + 80, y + self.ICON + 50], radius=10,
                                       outline=BLACK, width=3)
            if name in self.icons:
                image.paste(self.icons[name], (cx - self.ICON // 2, y))
            else:
                draw.rectangle([cx - 60, y, cx + 60, y + self.ICON], outline=BLACK, width=2)
            draw.text((cx - text_width(draw, name, label) // 2, y + self.ICON + 12), name, font=label, fill=BLACK)

        draw_hint(draw, self.width, self.height - 26, "Press: next app   |   Hold: open")
        draw_centered(draw, self.height - 24, f"Web: http://{get_ip_address()}:{self.web_port}", small, self.width)
        return image


class LibraryScreen:
    """Book list with covers. The last entry returns to the main menu."""

    def __init__(self, width: int, height: int, items_per_page: int = 4, web_port: int = 5000, battery=None):
        self.logger = logging.getLogger(__name__)
        self.width, self.height = width, height
        self.items_per_page = items_per_page
        self.web_port = web_port
        self.battery = battery
        self.sleep_enabled = True
        self.books: List[Dict[str, str]] = []
        self.current_index = 0
        self.covers = CoverExtractor()

    def load_books(self, books_dir: str):
        selected = self.get_selected_book()
        self.books = []
        if os.path.isdir(books_dir):
            for filename in os.listdir(books_dir):
                if filename.lower().endswith(".epub"):
                    self.books.append({"filename": filename, "path": os.path.join(books_dir, filename),
                                       "title": filename[:-5].replace("_", " ")})
        self.books.sort(key=lambda book: book["title"].lower())
        self.books.append({"filename": HOME_ENTRY, "path": HOME_ENTRY, "title": "Back to menu"})
        paths = [book["path"] for book in self.books]
        self.current_index = paths.index(selected["path"]) if selected and selected["path"] in paths else 0
        self.logger.info(f"Library: {len(self.books) - 1} books")

    def next_item(self):
        if self.books:
            self.current_index = (self.current_index + 1) % len(self.books)

    def prev_item(self):
        if self.books:
            self.current_index = (self.current_index - 1) % len(self.books)

    def get_selected_book(self) -> Optional[Dict[str, str]]:
        return self.books[self.current_index] if 0 <= self.current_index < len(self.books) else None

    def render(self) -> Image.Image:
        image = blank(self.width, self.height)
        draw = ImageDraw.Draw(image)
        draw_title_bar(draw, self.width, "Library", self.battery)
        body = font(20)
        if len(self.books) <= 1:
            y = 110
            for line in ("No books yet.", "Upload EPUB files from", f"http://{get_ip_address()}:{self.web_port}"):
                draw.text((40, y), line, font=body, fill=BLACK)
                y += 32

        per_page = max(1, self.items_per_page)
        row_height = (self.height - 80 - 64) // per_page
        cover_h = min(150, row_height - 12)
        cover_size = (cover_h * 2 // 3, cover_h)
        start = self.current_index // per_page * per_page
        y = 80
        for index in range(start, min(start + per_page, len(self.books))):
            book = self.books[index]
            if index == self.current_index:
                draw.rounded_rectangle([18, y - 4, self.width - 18, y + row_height - 8], radius=8,
                                       outline=BLACK, width=3)
            x = 32
            if book["path"] == HOME_ENTRY:
                image.paste(home_icon(cover_size), (x, y + 2))
            else:
                image.paste(self.covers.get_cover(book["path"], cover_size), (x, y + 2))
                draw.rectangle([x, y + 2, x + cover_size[0], y + 2 + cover_size[1]], outline=BLACK, width=1)
            text_x = x + cover_size[0] + 16
            max_lines = max(1, (row_height - 16) // 26)
            ty = y + 8
            for line in wrap_text(draw, book["title"], body, self.width - text_x - 32, max_lines=min(4, max_lines)):
                draw.text((text_x, ty), line, font=body, fill=BLACK)
                ty += 26
            y += row_height

        small = font(16)
        draw.line([(24, self.height - 48), (self.width - 24, self.height - 48)], fill=BLACK, width=1)
        draw.text((28, self.height - 36), f"Book {self.current_index + 1} of {len(self.books)}", font=small, fill=BLACK)
        status = "Sleep: on" if self.sleep_enabled else "Sleep: off"
        draw.text((self.width - 28 - text_width(draw, status, small), self.height - 36), status, font=small, fill=BLACK)
        return image


@lru_cache(maxsize=4)
def home_icon(size) -> Image.Image:
    """The "Back to menu" entry: a black tile the size of a book cover with a
    white house. Drawn at 4x and reduced, so the edges come out clean in 1 bit."""
    w, h = size
    scale = 4
    W, H = w * scale, h * scale
    tile = Image.new("L", (W, H), 255)
    draw = ImageDraw.Draw(tile)
    draw.rounded_rectangle([0, 0, W - 1, H - 1], radius=W // 10, fill=0)

    g = W * 0.64                       # House width
    cx, cy = W / 2, H / 2
    stroke = g * 0.13                  # Roof thickness
    apex = (cx, cy - g * 0.52)
    eave_y = cy - g * 0.02
    left, right = (cx - g / 2, eave_y), (cx + g / 2, eave_y)
    # Chimney first, so the roof covers its base.
    chimney_x = cx + g * 0.22
    draw.rectangle([chimney_x, cy - g * 0.46, chimney_x + g * 0.11, cy - g * 0.18], fill=255)
    draw.line([left, apex, right], fill=255, width=int(stroke), joint="curve")
    for end in (left, right, apex):  # Round the roof's ends
        r = stroke / 2
        draw.ellipse([end[0] - r, end[1] - r, end[0] + r, end[1] + r], fill=255)
    # Walls, with a door cut out.
    body_left, body_right = cx - g * 0.34, cx + g * 0.34
    body_top, body_bottom = cy - g * 0.12, cy + g * 0.46
    draw.rounded_rectangle([body_left, body_top, body_right, body_bottom], radius=g * 0.04, fill=255)
    door_w, door_top = g * 0.18, cy + g * 0.14
    draw.rounded_rectangle([cx - door_w / 2, door_top, cx + door_w / 2, body_bottom + 1],
                           radius=door_w / 2, fill=0)
    draw.rectangle([cx - door_w / 2, door_top + door_w / 2, cx + door_w / 2, body_bottom + 1], fill=0)

    return tile.resize((w, h), Image.Resampling.LANCZOS).point(lambda v: 255 if v >= 128 else 0, "1")


class ReaderScreen:
    """The open book. Rendered pages are cached; the footer (chapter, page and
    percentage, like InkDeck on ESP32) and the battery are drawn on top at
    display time, so settings changes and battery levels never go stale."""

    FOOTER_Y = 26  # Footer text top, from the bottom edge

    def __init__(self, width: int, height: int, zoom_factor: float = 1.0, cache_size: int = 3, battery=None):
        self.logger = logging.getLogger(__name__)
        self.width, self.height = width, height
        self.zoom_factor = zoom_factor
        self.font_family = "serif"
        self.book_fonts = False
        self.show_chapter = True
        self.show_page_numbers = True
        self.show_percentage = True
        self.battery = battery
        self.cache_size = cache_size
        self.renderer = None
        self.page_cache = None
        self.current_page = 0
        self.current_book_path = None

    @property
    def is_open(self) -> bool:
        return self.renderer is not None

    def load_epub(self, epub_path: str, progress_callback=None):
        from ..reader.renderer import PillowTextRenderer
        self.close()
        self.renderer = PillowTextRenderer(epub_path, width=self.width, height=self.height,
                                           zoom_factor=self.zoom_factor, progress_callback=progress_callback,
                                           font_family=self.font_family, book_fonts=self.book_fonts)
        self.page_cache = PageCache(self.cache_size)
        self.current_page = 0
        self.current_book_path = epub_path
        self.logger.info(f"Opened {os.path.basename(epub_path)}: {self.page_count} pages")

    @property
    def page_count(self) -> int:
        return self.renderer.get_page_count() if self.renderer else 0

    def next_page(self) -> bool:
        if self.renderer and self.current_page < self.page_count - 1:
            self.current_page += 1
            return True
        return False

    def prev_page(self) -> bool:
        if self.renderer and self.current_page > 0:
            self.current_page -= 1
            return True
        return False

    def go_to_page(self, page: int):
        if self.renderer:
            self.current_page = max(0, min(page, self.page_count - 1))

    @property
    def chapters(self) -> list:
        return self.renderer.chapters if self.renderer else []

    def chapter_title(self, page: int = None) -> str:
        """The chapter a page is in ("" before the first chapter or without a table of contents)."""
        chapter = self.renderer.chapter_at(self.current_page if page is None else page) if self.renderer else None
        return chapter.title if chapter else ""

    def percent_read(self) -> int:
        return round((self.current_page + 1) * 100 / self.page_count) if self.page_count else 0

    def get_current_image(self) -> Image.Image:
        if not self.renderer:
            return blank(self.width, self.height)
        page = self.page_cache.get(self.current_page)
        if page is None:
            page = self.renderer.render_page(self.current_page)
            self.page_cache.put(self.current_page, page)
        page = page.copy()
        draw = ImageDraw.Draw(page)
        self._draw_footer(draw)
        if self.battery is not None:
            draw.rectangle([self.width - 96, 2, self.width - 2, 24], fill=WHITE)
            draw_battery(draw, self.width - 10, 6, self.battery)
        return page

    def _draw_footer(self, draw: ImageDraw.ImageDraw):
        """Chapter on the left, page in the middle, percentage on the right."""
        fnt, margin, gap = font(15), 16, 14
        y = self.height - self.FOOTER_Y
        right = self.width - margin
        if self.show_percentage:
            label = f"{self.percent_read()}%"
            right -= text_width(draw, label, fnt)
            draw.text((right, y), label, font=fnt, fill=BLACK)
            right -= gap
        if self.show_page_numbers:
            label = f"Page {self.current_page + 1} of {self.page_count}"
            width = text_width(draw, label, fnt)
            x = min((self.width - width) // 2, right - width)
            draw.text((x, y), label, font=fnt, fill=BLACK)
            right = x - gap
        title = self.chapter_title() if self.show_chapter else ""
        if title:
            lines = wrap_text(draw, title, fnt, max(40, right - margin), max_lines=1)
            draw.text((margin, y), lines[0], font=fnt, fill=BLACK)

    def prerender_next(self):
        """Render the next page ahead of time, so the next page turn is quicker."""
        nxt = self.current_page + 1
        if self.renderer and nxt < self.page_count and nxt not in self.page_cache:
            self.page_cache.put(nxt, self.renderer.render_page(nxt))

    def loading_image(self, percentage: float, message: str) -> Image.Image:
        image = blank(self.width, self.height)
        draw = ImageDraw.Draw(image)
        draw_centered(draw, self.height // 2 - 64, message, font(24), self.width)
        bar_w, bar_h = min(400, self.width - 60), 30
        x, y = (self.width - bar_w) // 2, self.height // 2
        draw.rectangle([x, y, x + bar_w, y + bar_h], outline=BLACK, width=2)
        fill = int((bar_w - 4) * max(0, min(100, percentage)) / 100)
        if fill > 0:
            draw.rectangle([x + 2, y + 2, x + 2 + fill, y + bar_h - 2], fill=BLACK)
        draw_centered(draw, y + bar_h + 20, f"{int(percentage)}%", font(18), self.width)
        return image

    def close(self):
        self.renderer = None
        self.page_cache = None
        self.current_book_path = None
