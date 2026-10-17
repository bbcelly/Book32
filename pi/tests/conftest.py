"""Test setup: data goes to a temporary folder, and EPUBs are built on the fly."""

import io
import os
import sys
import tempfile
import zipfile
from pathlib import Path

import pytest

# Must be set before inkdeck.paths is imported.
os.environ["INKDECK_DATA"] = tempfile.mkdtemp(prefix="inkdeck-test-")
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

# Tests never drive the real panel or GPIO, even on a Pi.
sys.modules["waveshare_epd"] = None
sys.modules["gpiozero"] = None

from PIL import Image  # noqa: E402


def make_epub(path: Path, title="Test Book", chapters=3, paragraphs=40, image=True) -> Path:
    """A small valid EPUB 2 with a cover image, text chapters and an inline image."""
    cover = io.BytesIO()
    Image.new("RGB", (300, 450), (40, 80, 160)).save(cover, "PNG")
    body = " ".join(["The quick brown fox jumps over the lazy dog."] * 6)
    manifest = ['<item id="cover-img" href="images/cover.png" media-type="image/png"/>']
    spine = []
    files = {}
    for n in range(chapters):
        extra = '<p><img src="../images/cover.png" alt=""/></p>' if image and n == 0 else ""
        files[f"OEBPS/text/ch{n}.xhtml"] = (
            '<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>c</title></head>'
            f"<body><h1>Chapter {n + 1}</h1>{extra}" + "".join(f"<p>{body} <b>Bold</b> <i>italic</i></p>"
                                                                 for _ in range(paragraphs)) +
            "<table><tr><td>A</td><td>B</td></tr><tr><td>1</td><td>2</td></tr></table></body></html>")
        manifest.append(f'<item id="ch{n}" href="text/ch{n}.xhtml" media-type="application/xhtml+xml"/>')
        spine.append(f'<itemref idref="ch{n}"/>')
    manifest.append('<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>')
    opf = ('<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">'
           '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
           f'<dc:title>{title}</dc:title><dc:identifier id="id">test-{title}</dc:identifier><dc:language>en</dc:language>'
           '<meta name="cover" content="cover-img"/></metadata>'
           f'<manifest>{"".join(manifest)}</manifest><spine toc="ncx">{"".join(spine)}</spine></package>')
    ncx = ('<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/>'
           '<docTitle><text>t</text></docTitle><navMap><navPoint id="n0" playOrder="1"><navLabel><text>1</text>'
           '</navLabel><content src="text/ch0.xhtml"/></navPoint></navMap></ncx>')
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml",
                   '<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
                   '<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>'
                   '</rootfiles></container>')
        z.writestr("OEBPS/content.opf", opf)
        z.writestr("OEBPS/toc.ncx", ncx)
        z.writestr("OEBPS/images/cover.png", cover.getvalue())
        for name, text in files.items():
            z.writestr(name, text)
    return path


@pytest.fixture
def epub(tmp_path):
    return make_epub(tmp_path / "Test_Book.epub")
