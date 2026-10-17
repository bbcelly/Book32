"""Least-recently-used cache of rendered pages, small for the Pi Zero's RAM."""

from collections import OrderedDict
from typing import Optional

from PIL import Image


class PageCache:
    def __init__(self, max_size: int = 3):
        self.max_size = max(1, max_size)
        self._pages: "OrderedDict[int, Image.Image]" = OrderedDict()
        self.hits = self.misses = 0

    def get(self, page: int) -> Optional[Image.Image]:
        image = self._pages.get(page)
        if image is None:
            self.misses += 1
            return None
        self._pages.move_to_end(page)
        self.hits += 1
        return image

    def put(self, page: int, image: Image.Image):
        self._pages[page] = image
        self._pages.move_to_end(page)
        while len(self._pages) > self.max_size:
            self._pages.popitem(last=False)

    def __contains__(self, page: int) -> bool:
        return page in self._pages

    def clear(self):
        self._pages.clear()

    def hit_rate(self) -> float:
        total = self.hits + self.misses
        return self.hits / total * 100 if total else 0.0
