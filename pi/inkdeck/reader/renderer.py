"""EPUB renderer: flows a book's text into pages and draws them with TrueType
fonts as 1-bit images, sharp on e-paper.

Basic formatting is kept: bold, italic, headings, centering, images (SVG via
cairosvg when installed) and simple tables. Page layout is cached in
data/cache/, so a book opens quickly the second time. Images are decoded only
when a page that shows them is drawn.
"""

import hashlib
import io
import logging
import os
import pickle
import re
from typing import List, NamedTuple, Union

import ebooklib
from bs4 import BeautifulSoup, NavigableString, Tag
from ebooklib import epub
from PIL import Image, ImageDraw, ImageFont

from ..paths import CACHE_DIR

try:
    import cairosvg
except ImportError:
    cairosvg = None

# Bump when the layout or the cache format changes, so old caches are ignored.
LAYOUT_VERSION = 2

_FONT_FAMILIES = [
    {"normal": "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
     "bold": "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
     "italic": "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Italic.ttf",
     "bold_italic": "/usr/share/fonts/truetype/dejavu/DejaVuSerif-BoldItalic.ttf"},
    {"normal": "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf",
     "bold": "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf",
     "italic": "/usr/share/fonts/truetype/liberation/LiberationSerif-Italic.ttf",
     "bold_italic": "/usr/share/fonts/truetype/liberation/LiberationSerif-BoldItalic.ttf"},
]
_BLOCK_TAGS = {"p", "div", "h1", "h2", "h3", "h4", "br", "li"}
LAYOUT_DIR = CACHE_DIR / "layout"


def _book_id(epub_path: str) -> str:
    return hashlib.sha1(os.path.basename(epub_path).encode()).hexdigest()[:12]


def forget_layouts(epub_path: str):
    """Delete a book's cached layouts (after the book is deleted)."""
    for file in LAYOUT_DIR.glob(f"{_book_id(epub_path)}-*.pickle"):
        file.unlink(missing_ok=True)


class TextToken(NamedTuple):
    text: str
    style: str  # normal, bold, italic, bold_italic, h1, h2
    new_paragraph: bool = False
    align: str = "left"


class ImageToken(NamedTuple):
    name: str
    width: int
    height: int
    new_paragraph: bool = True


class TableToken(NamedTuple):
    rows: List[List[str]]
    new_paragraph: bool = True


Token = Union[TextToken, ImageToken, TableToken]


class PillowTextRenderer:
    def __init__(self, epub_path: str, width: int = 480, height: int = 800,
                 zoom_factor: float = 1.0, progress_callback=None):
        self.logger = logging.getLogger(__name__)
        self.epub_path = epub_path
        self.width = width
        self.height = height
        self.zoom_factor = zoom_factor
        self.progress = progress_callback or (lambda percent, message: None)

        self.margin_left = self.margin_right = self.margin_top = 30
        self.margin_bottom = 40
        self.line_spacing = 1.3
        self.paragraph_spacing = 5
        self.paragraph_indent = 40
        self.base_font_size = int(18 * zoom_factor)
        self.header_font_size = int(24 * zoom_factor)
        self.text_width = width - self.margin_left - self.margin_right
        self.text_height = height - self.margin_top - self.margin_bottom

        self.fonts = {}
        self.pages = []  # Each page: list of (x, y, text, style) or (x, y, data, 'IMAGE'|'TABLE', w, h)
        self.page_count = 0
        self.book = None
        self._image_items = {}  # EPUB image name -> ebooklib item
        self._decoded = {}      # EPUB image name -> PIL image

        self._load_fonts()
        self._load_epub()

    # ---- Fonts ----

    def _load_fonts(self):
        family = next((f for f in _FONT_FAMILIES if os.path.exists(f["normal"])), None)

        def load(style, size):
            if family:
                path = family[style] if os.path.exists(family[style]) else family["normal"]
                try:
                    return ImageFont.truetype(path, size)
                except OSError:
                    pass
            return ImageFont.load_default(size)

        for style in ("normal", "bold", "italic", "bold_italic"):
            self.fonts[style] = load(style, self.base_font_size)
        self.fonts["h1"] = load("bold", self.header_font_size)
        self.fonts["h2"] = load("bold", int(self.header_font_size * 0.9))

    def _use_embedded_fonts(self):
        """Use the book's own TTF/OTF fonts, saved once to data/cache/fonts/."""
        fonts = [item for item in self.book.get_items()
                 if item.get_name().lower().endswith((".ttf", ".otf"))]
        font_dir = CACHE_DIR / "fonts"
        for item in fonts:
            data = item.get_content()
            path = font_dir / (hashlib.sha1(data).hexdigest()[:16] + os.path.splitext(item.get_name())[1])
            try:
                if not path.exists():
                    font_dir.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
                name = os.path.basename(item.get_name()).lower()
                if "bolditalic" in name or "bold-italic" in name:
                    self.fonts["bold_italic"] = ImageFont.truetype(str(path), self.base_font_size)
                elif "bold" in name:
                    self.fonts["bold"] = ImageFont.truetype(str(path), self.base_font_size)
                    self.fonts["h1"] = ImageFont.truetype(str(path), self.header_font_size)
                    self.fonts["h2"] = ImageFont.truetype(str(path), int(self.header_font_size * 0.9))
                elif "italic" in name:
                    self.fonts["italic"] = ImageFont.truetype(str(path), self.base_font_size)
                elif "regular" in name or "normal" in name or len(fonts) == 1:
                    self.fonts["normal"] = ImageFont.truetype(str(path), self.base_font_size)
            except OSError as e:
                self.logger.warning(f"Embedded font {item.get_name()} not usable: {e}")

    # ---- Loading ----

    def _cache_path(self):
        stat = os.stat(self.epub_path)
        stamp = f"{stat.st_size:x}{int(stat.st_mtime):x}"
        return LAYOUT_DIR / (f"{_book_id(self.epub_path)}-{stamp}.{self.width}x{self.height}"
                             f".z{self.zoom_factor:g}.v{LAYOUT_VERSION}.pickle")

    def _load_epub(self):
        self.progress(5, "Opening book...")
        self.book = epub.read_epub(self.epub_path, options={"ignore_ncx": True})
        self._image_items = {item.get_name(): item for item in self.book.get_items_of_type(ebooklib.ITEM_IMAGE)}
        self._use_embedded_fonts()

        cache = self._cache_path()
        try:
            with open(cache, "rb") as f:
                data = pickle.load(f)
            self.pages, self.page_count = data["pages"], data["page_count"]
            self.logger.info(f"Layout loaded from cache: {self.page_count} pages")
            return
        except (OSError, pickle.UnpicklingError, KeyError, EOFError, AttributeError):
            pass

        docs = list(self.book.get_items_of_type(ebooklib.ITEM_DOCUMENT))
        tokens: List[Token] = []
        for index, item in enumerate(docs):
            self.progress(15 + 65 * index / max(1, len(docs)), f"Reading chapter {index + 1} of {len(docs)}...")
            try:
                html = item.get_content().decode("utf-8", errors="replace")
                chapter = self._parse_html(html)
            except Exception as e:
                self.logger.warning(f"Skipping chapter {item.get_name()}: {e}")
                continue
            if chapter:
                tokens.extend(chapter)
                tokens.append(TextToken("", "normal", new_paragraph=True))

        self.progress(85, "Laying out pages...")
        self._reflow_pages(tokens)
        try:
            cache.parent.mkdir(parents=True, exist_ok=True)
            stamp = cache.name.split(".")[0]
            for old in cache.parent.glob(f"{_book_id(self.epub_path)}-*.pickle"):
                if not old.name.startswith(stamp + "."):
                    old.unlink(missing_ok=True)  # Layouts of an older copy of this book
            with open(cache, "wb") as f:
                pickle.dump({"pages": self.pages, "page_count": self.page_count}, f)
        except OSError as e:
            self.logger.warning(f"Could not cache layout: {e}")
        self.logger.info(f"Laid out {self.page_count} pages")

    # ---- Images ----

    def _find_image(self, src: str):
        """The EPUB image name for an <img src>, matched by file name."""
        filename = src.split("/")[-1].split("#")[0]
        for name in self._image_items:
            if name.endswith(filename):
                return name
        return None

    def _image(self, name: str):
        """Decode an EPUB image to grayscale on a white background (cached)."""
        if name in self._decoded:
            return self._decoded[name]
        item = self._image_items.get(name)
        image = None
        if item is not None:
            data = item.get_content()
            try:
                if name.lower().endswith(".svg"):
                    if cairosvg is None:
                        return None
                    data = cairosvg.svg2png(bytestring=data, output_width=800)
                image = Image.open(io.BytesIO(data))
                if image.mode in ("RGBA", "LA", "P"):
                    image = image.convert("RGBA")
                    background = Image.new("RGBA", image.size, (255, 255, 255, 255))
                    image = Image.alpha_composite(background, image)
                image = image.convert("L")
            except Exception as e:
                self.logger.warning(f"Image {name} not readable: {e}")
                image = None
        if len(self._decoded) > 8:
            self._decoded.clear()
        self._decoded[name] = image
        return image

    def _image_size(self, name: str):
        item = self._image_items[name]
        if name.lower().endswith(".svg"):
            image = self._image(name)
            return image.size if image else None
        try:
            return Image.open(io.BytesIO(item.get_content())).size  # Reads the header only
        except Exception:
            return None

    # ---- Parsing ----

    def _parse_html(self, html: str) -> List[Token]:
        soup = BeautifulSoup(html, "html.parser")
        for tag in soup(["head", "script", "style", "title", "meta"]):
            tag.decompose()
        tokens: List[Token] = []

        def end_paragraph():
            if tokens and isinstance(tokens[-1], TextToken) and not tokens[-1].new_paragraph:
                tokens[-1] = tokens[-1]._replace(new_paragraph=True)

        def visit(node, style="normal", align="left"):
            if isinstance(node, NavigableString):
                text = str(node).replace("\n", " ")
                if text.strip():
                    tokens.extend(TextToken(w, style, False, align) for w in re.split(r"(\s+)", text) if w)
                return
            if not isinstance(node, Tag):
                return
            if node.name == "table":
                rows = [[cell.get_text(" ", strip=True) for cell in tr.find_all(["td", "th"])]
                        for tr in node.find_all("tr")]
                rows = [row for row in rows if row]
                if rows:
                    tokens.append(TableToken(rows))
                return
            if node.name in ("img", "image"):
                src = node.get("src") or node.get("xlink:href") or node.get("href") or ""
                name = self._find_image(src) if src else None
                size = self._image_size(name) if name else None
                if size:
                    tokens.append(ImageToken(name, *size))
                elif src:
                    self.logger.debug(f"Image not found in book: {src}")
                return

            css = node.get("style", "")
            if "text-align" in css:
                align = "center" if "center" in css else "right" if "right" in css else "left"
            if node.name == "center":
                align = "center"
            if node.name in ("b", "strong"):
                style = "bold_italic" if "italic" in style else "bold"
            elif node.name in ("i", "em"):
                style = "bold_italic" if "bold" in style else "italic"
            elif node.name in ("h1", "h2"):
                style, align = node.name, "center"
            elif node.name in ("h3", "h4"):
                style = "bold"

            block = node.name in _BLOCK_TAGS
            if block:
                end_paragraph()
            for child in node.children:
                visit(child, style, align)
            if block:
                end_paragraph()

        visit(soup.body or soup)
        return tokens

    # ---- Layout ----

    def _reflow_pages(self, tokens: List[Token]):
        self.pages = []
        page = []
        bottom = self.height - self.margin_bottom
        y = self.margin_top
        x = self.margin_left + self.paragraph_indent
        line, line_height = [], 0
        heights = {style: (f.getbbox("Ay")[3] - f.getbbox("Ay")[1]) for style, f in self.fonts.items()}

        def new_page():
            nonlocal page, y
            self.pages.append(page)
            page, y = [], self.margin_top

        def finish_line(height):
            nonlocal y, line, line_height
            if y + height > bottom:
                new_page()
            page.extend((lx, y, text, style) for text, style, lx in line)
            y += int(height * self.line_spacing)
            line, line_height = [], 0

        for count, token in enumerate(tokens, 1):
            if count % 2000 == 0:
                self.progress(85 + 14 * count / len(tokens), "Laying out pages...")

            if isinstance(token, TableToken):
                if line:
                    finish_line(line_height or self.base_font_size)
                columns = max(len(row) for row in token.rows)
                col_width = self.text_width // columns
                row_height = int(self.base_font_size * 1.5)
                table_height = row_height * len(token.rows)
                if y + table_height > bottom and page:
                    new_page()
                page.append((self.margin_left, y, token.rows, "TABLE", col_width, row_height))
                y += table_height + self.paragraph_spacing * 2
                x = self.margin_left + self.paragraph_indent
                continue

            if isinstance(token, ImageToken):
                if line:
                    finish_line(line_height or self.base_font_size)
                scale = min(self.text_width / token.width, self.text_height * 0.6 / token.height, 1.0)
                w, h = max(1, int(token.width * scale)), max(1, int(token.height * scale))
                if y + h > bottom and page:
                    new_page()
                page.append((self.margin_left + (self.text_width - w) // 2, y, token.name, "IMAGE", w, h))
                y += h + self.paragraph_spacing * 2
                x = self.margin_left + self.paragraph_indent
                continue

            fnt = self.fonts.get(token.style, self.fonts["normal"])
            font_height = heights.get(token.style, self.base_font_size)
            if token.style in ("h1", "h2"):
                if line:
                    finish_line(line_height or font_height)
                x = self.margin_left
                y += self.paragraph_spacing * 2

            width = fnt.getlength(token.text)
            if x + width > self.width - self.margin_right:
                finish_line(line_height or font_height)
                x = self.margin_left
                if token.text.isspace():
                    continue
            line.append((token.text, token.style, x))
            x += width
            line_height = max(line_height, font_height)

            if token.new_paragraph:
                finish_line(line_height or font_height)
                x = self.margin_left + self.paragraph_indent
                y += self.paragraph_spacing

        if line:
            finish_line(line_height)
        if page:
            self.pages.append(page)
        self.pages = self.pages or [[]]
        self.page_count = len(self.pages)

    # ---- Drawing ----

    def render_page(self, page_num: int, show_page_number: bool = True) -> Image.Image:
        image = Image.new("1", (self.width, self.height), 1)
        draw = ImageDraw.Draw(image)
        normal = self.fonts["normal"]
        for item in self.pages[page_num] if 0 <= page_num < len(self.pages) else []:
            if len(item) == 4:
                x, y, text, style = item
                draw.text((x, y), text, font=self.fonts.get(style, normal), fill=0)
                continue
            x, y, data, kind, w, h = item
            if kind == "IMAGE":
                source = self._image(data)
                if source is not None:
                    picture = source.resize((w, h), Image.Resampling.LANCZOS)
                    image.paste(picture.convert("1", dither=Image.Dither.FLOYDSTEINBERG), (x, y))
            elif kind == "TABLE":
                columns = max(len(row) for row in data)
                table_w, table_h = w * columns, h * len(data)
                draw.rectangle([x, y, x + table_w, y + table_h], outline=0, width=2)
                max_chars = max(4, int(w / (self.base_font_size * 0.6)))
                for r, row in enumerate(data):
                    row_y = y + r * h
                    if r:
                        draw.line([(x, row_y), (x + table_w, row_y)], fill=0, width=1)
                    for c, cell in enumerate(row):
                        col_x = x + c * w
                        if c:
                            draw.line([(col_x, y), (col_x, y + table_h)], fill=0, width=1)
                        text = cell if len(cell) <= max_chars else cell[:max_chars - 1] + "…"
                        draw.text((col_x + 5, row_y + (h - self.base_font_size) // 2), text, font=normal, fill=0)
        if show_page_number:
            label = f"Page {page_num + 1} of {self.page_count}"
            draw.text(((self.width - draw.textlength(label, font=normal)) // 2, self.height - 25),
                      label, font=normal, fill=0)
        return image

    def get_page_count(self) -> int:
        return self.page_count
