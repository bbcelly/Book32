"""Where InkDeck keeps its code, its configuration and the user's data.

Everything is relative to the app folder (the folder holding `inkdeck/`), so
the app runs from any install location and under any user name. User data
(settings, reading positions, to-dos, covers and layout caches) lives in
`data/`, which updates never touch; set INKDECK_DATA to move it.
"""

import json
import os
import tempfile
from pathlib import Path

APP_ROOT = Path(__file__).resolve().parent.parent
ASSETS_DIR = APP_ROOT / "assets"
SCRIPTS_DIR = APP_ROOT / "scripts"
DATA_DIR = Path(os.environ.get("INKDECK_DATA", APP_ROOT / "data"))
CACHE_DIR = DATA_DIR / "cache"


def resolve(path) -> Path:
    """A configured path: ~ and $VARS expand, relative paths start at the app folder."""
    expanded = Path(os.path.expandvars(os.path.expanduser(str(path))))
    return expanded if expanded.is_absolute() else APP_ROOT / expanded


def data_path(*parts) -> Path:
    """A file or folder under data/, creating the parent folder."""
    path = DATA_DIR.joinpath(*parts)
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def load_json(path, default):
    """Read a JSON file, or return `default` when it is missing or unreadable."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return default


def save_json(path, data):
    """Write a JSON file atomically, so a power cut never leaves half a file."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=path.name, suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise
