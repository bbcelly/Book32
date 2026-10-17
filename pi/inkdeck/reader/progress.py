"""Saved reading positions, kept in data/reading_progress.json.

Positions are keyed by the book's file name, so they survive moving the
books folder. Older files keyed by full path are converted on load.
"""

import logging
import os
import threading
from datetime import datetime
from typing import Dict, Optional

from ..paths import data_path, load_json, save_json


def book_key(book_path: str) -> str:
    return os.path.basename(book_path)


class ProgressManager:
    def __init__(self, path=None):
        self.logger = logging.getLogger(__name__)
        self.path = path or data_path("reading_progress.json")
        self._lock = threading.Lock()
        stored = load_json(self.path, {})
        self._data: Dict[str, dict] = {}
        for key, entry in (stored.items() if isinstance(stored, dict) else ()):
            if isinstance(entry, dict) and "current_page" in entry:
                self._data[book_key(key)] = entry

    def save_progress(self, book_path: str, current_page: int, total_pages: int, chapter: str = ""):
        with self._lock:
            self._data[book_key(book_path)] = {
                "current_page": current_page,
                "total_pages": total_pages,
                "chapter": chapter,
                "last_read": datetime.now().isoformat(timespec="seconds"),
            }
            save_json(self.path, self._data)

    def load_progress(self, book_path: str, total_pages: int = None) -> Optional[int]:
        """The saved page. When the book now has a different number of pages (a new
        text size or layout), the page at the same point in the book."""
        with self._lock:
            entry = self._data.get(book_key(book_path))
        if not entry:
            return None
        page, saved_total = entry["current_page"], entry.get("total_pages")
        if total_pages and saved_total and saved_total != total_pages:
            page = round(page * total_pages / saved_total)
        return page

    def clear_progress(self, book_path: str):
        with self._lock:
            if self._data.pop(book_key(book_path), None) is not None:
                save_json(self.path, self._data)

    def clear_all_progress(self):
        with self._lock:
            self._data = {}
            save_json(self.path, self._data)

    def get_all_progress(self) -> Dict[str, dict]:
        with self._lock:
            return {key: dict(value) for key, value in self._data.items()}
