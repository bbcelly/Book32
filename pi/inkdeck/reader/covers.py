"""EPUB cover thumbnails for the library, dithered to 1-bit and cached in
data/covers/ (and in memory while the app runs)."""

import io
import logging
import posixpath
import re
import zipfile
from urllib.parse import unquote
from pathlib import Path
from typing import Optional, Tuple

from PIL import Image, ImageDraw, ImageEnhance

from ..paths import DATA_DIR

logger = logging.getLogger(__name__)
_IMAGE_EXTS = (".jpg", ".jpeg", ".png", ".gif")


def _find_cover_name(epub: zipfile.ZipFile) -> Optional[str]:
    """The cover image's path in the EPUB: the OPF's cover declaration first,
    then any image whose name contains 'cover'."""
    names = epub.namelist()
    try:
        container = epub.read("META-INF/container.xml").decode("utf-8", "ignore")
        opf_path = re.search(r'full-path="([^"]+)"', container).group(1)
        opf = epub.read(opf_path).decode("utf-8", "ignore")
        base = posixpath.dirname(opf_path)
        items = {}
        for tag in re.findall(r"<item\b[^>]*>", opf):
            attrs = dict(re.findall(r'([\w:-]+)="([^"]*)"', tag))
            if "href" in attrs:
                items[attrs.get("id")] = attrs
                if "cover-image" in attrs.get("properties", ""):  # EPUB 3
                    return posixpath.normpath(posixpath.join(base, unquote(attrs["href"])))
        meta = re.search(r'<meta\b[^>]*name="cover"[^>]*content="([^"]+)"', opf) or \
            re.search(r'<meta\b[^>]*content="([^"]+)"[^>]*name="cover"', opf)  # EPUB 2
        if meta and meta.group(1) in items:
            return posixpath.normpath(posixpath.join(base, unquote(items[meta.group(1)]["href"])))
    except (KeyError, AttributeError, zipfile.BadZipFile):
        pass
    for name in names:
        if "cover" in name.lower() and name.lower().endswith(_IMAGE_EXTS):
            return name
    return None


class CoverExtractor:
    def __init__(self, cache_dir: Path = None):
        self.cache_dir = Path(cache_dir or DATA_DIR / "covers")
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        self._memory = {}

    def get_cover(self, epub_path: str, size: Tuple[int, int]) -> Image.Image:
        """The book's cover thumbnail, or a drawn book when it has none."""
        key = (epub_path, size)
        if key in self._memory:
            return self._memory[key]
        cache_file = self.cache_dir / f"{Path(epub_path).stem}_{size[0]}x{size[1]}.png"
        cover = None
        if cache_file.exists() and cache_file.stat().st_mtime >= Path(epub_path).stat().st_mtime:
            try:
                cover = Image.open(cache_file).convert("1")
            except OSError:
                cover = None
        if cover is None:
            source = self._extract(epub_path)
            if source is not None:
                cover = self._thumbnail(source, size)
                try:
                    cover.save(cache_file)
                except OSError as e:
                    logger.warning(f"Could not cache cover: {e}")
        cover = cover or placeholder_cover(size)
        self._memory[key] = cover
        return cover

    def forget(self, epub_path: str):
        """Drop a book's cached thumbnails (after it is deleted)."""
        self._memory = {k: v for k, v in self._memory.items() if k[0] != epub_path}
        for file in self.cache_dir.glob(f"{Path(epub_path).stem}_*.png"):
            file.unlink(missing_ok=True)

    @staticmethod
    def _extract(epub_path: str) -> Optional[Image.Image]:
        try:
            with zipfile.ZipFile(epub_path) as epub:
                name = _find_cover_name(epub)
                if name:
                    image = Image.open(io.BytesIO(epub.read(name)))
                    image.load()
                    return image
        except (OSError, KeyError, zipfile.BadZipFile) as e:
            logger.warning(f"No cover from {Path(epub_path).name}: {e}")
        return None

    @staticmethod
    def _thumbnail(image: Image.Image, size: Tuple[int, int]) -> Image.Image:
        image = image.convert("L")
        image = ImageEnhance.Contrast(image).enhance(1.5)
        image.thumbnail(size, Image.Resampling.LANCZOS)
        canvas = Image.new("L", size, 255)
        canvas.paste(image, ((size[0] - image.width) // 2, (size[1] - image.height) // 2))
        return canvas.convert("1", dither=Image.Dither.FLOYDSTEINBERG)


def placeholder_cover(size: Tuple[int, int]) -> Image.Image:
    """A simple drawn book for EPUBs without a cover image."""
    image = Image.new("1", size, 1)
    draw = ImageDraw.Draw(image)
    w, h = size
    draw.rectangle([4, 4, w - 5, h - 5], outline=0, width=2)
    spine = w // 4
    draw.line([(spine, 4), (spine, h - 5)], fill=0, width=2)
    for y in range(h // 4, h * 3 // 4, max(8, h // 8)):
        draw.line([(spine + 8, y), (w - 12, y)], fill=0, width=1)
    return image
