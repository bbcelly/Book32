"""Terminal and word processor for a keyboard, with two tabs.

Alt switches tabs and Esc returns to the menu. The terminal runs shell
commands on the Pi (30 second limit). The word processor saves plain text
files to data/documents/ with Ctrl+S; Ctrl+N starts a new document.
"""

import logging
import os
import subprocess
from datetime import datetime
from typing import List, Optional, Tuple

from PIL import Image, ImageDraw

from ...paths import DATA_DIR
from ...ui.common import BLACK, blank, draw_battery, font

try:
    from evdev import ecodes
except ImportError:
    ecodes = None


class TypewriterScreen:
    MODE_TERMINAL, MODE_WORDPROC = 0, 1
    HEADER, FOOTER, MARGIN = 50, 30, 20

    def __init__(self, width: int, height: int, font_size: int = 16, battery=None):
        self.logger = logging.getLogger(__name__)
        self.width, self.height = width, height
        self.battery = battery
        self.mode = self.MODE_TERMINAL
        self.mono = font(font_size, family="mono")
        self.mono_bold = font(font_size, bold=True, family="mono")
        box = self.mono.getbbox("M")
        self.char_width = max(1, box[2] - box[0])
        self.char_height = box[3] - box[1] + 4
        self.visible_lines = (height - self.HEADER - self.FOOTER) // self.char_height
        self.chars_per_line = (width - 2 * self.MARGIN) // self.char_width

        self.terminal_input = ""
        self.terminal_history: List[Tuple[str, str]] = []
        self.terminal_scroll = 0
        self.current_dir = os.path.expanduser("~")

        self.document_lines = [""]
        self.cursor_line = self.cursor_col = self.wp_scroll = 0
        self.document_dir = DATA_DIR / "documents"
        self.current_document: Optional[str] = None

    def toggle_mode(self):
        self.mode = self.MODE_WORDPROC if self.mode == self.MODE_TERMINAL else self.MODE_TERMINAL

    # ---- Terminal ----

    def _terminal_lines(self) -> List[Tuple[str, str]]:
        lines = []
        prompt = f"{os.path.basename(self.current_dir) or '/'}$ "
        for command, output in self.terminal_history:
            lines.append(("prompt", prompt + command))
            for out in output.split("\n"):
                while len(out) > self.chars_per_line:
                    lines.append(("output", out[:self.chars_per_line]))
                    out = out[self.chars_per_line:]
                lines.append(("output", out))
        lines.append(("prompt", f"{prompt}{self.terminal_input}_"))
        return lines

    def terminal_execute(self):
        command = self.terminal_input.strip()
        self.terminal_input = ""
        if not command:
            return
        if command == "clear":
            self.terminal_history = []
            self.terminal_scroll = 0
            return
        if command == "cd" or command.startswith("cd "):
            target = os.path.expanduser(command[3:].strip() or "~")
            target = os.path.abspath(os.path.join(self.current_dir, target))
            if os.path.isdir(target):
                self.current_dir = target
                output = target
            else:
                output = f"No such directory: {target}"
        else:
            try:
                result = subprocess.run(command, shell=True, cwd=self.current_dir,
                                        capture_output=True, text=True, timeout=30)
                output = (result.stdout + result.stderr).strip() or "(no output)"
            except subprocess.TimeoutExpired:
                output = "Command timed out (30 s limit)"
            except OSError as e:
                output = f"Error: {e}"
        self.terminal_history = self.terminal_history[-50:] + [(command, output)]
        self.terminal_scroll = max(0, len(self._terminal_lines()) - self.visible_lines)

    # ---- Word processor ----

    def _keep_cursor_visible(self):
        if self.cursor_line < self.wp_scroll:
            self.wp_scroll = self.cursor_line
        elif self.cursor_line >= self.wp_scroll + self.visible_lines:
            self.wp_scroll = self.cursor_line - self.visible_lines + 1

    def wp_type(self, char: str):
        line = self.document_lines[self.cursor_line]
        self.document_lines[self.cursor_line] = line[:self.cursor_col] + char + line[self.cursor_col:]
        self.cursor_col += len(char)

    def wp_backspace(self):
        if self.cursor_col > 0:
            line = self.document_lines[self.cursor_line]
            self.document_lines[self.cursor_line] = line[:self.cursor_col - 1] + line[self.cursor_col:]
            self.cursor_col -= 1
        elif self.cursor_line > 0:
            previous = self.document_lines[self.cursor_line - 1]
            self.cursor_col = len(previous)
            self.document_lines[self.cursor_line - 1] = previous + self.document_lines.pop(self.cursor_line)
            self.cursor_line -= 1
            self._keep_cursor_visible()

    def wp_enter(self):
        line = self.document_lines[self.cursor_line]
        self.document_lines[self.cursor_line] = line[:self.cursor_col]
        self.document_lines.insert(self.cursor_line + 1, line[self.cursor_col:])
        self.cursor_line += 1
        self.cursor_col = 0
        self._keep_cursor_visible()

    def wp_move(self, d_line: int, d_col: int):
        if d_line:
            self.cursor_line = max(0, min(len(self.document_lines) - 1, self.cursor_line + d_line))
            self.cursor_col = min(self.cursor_col, len(self.document_lines[self.cursor_line]))
        elif d_col < 0:
            if self.cursor_col > 0:
                self.cursor_col -= 1
            elif self.cursor_line > 0:
                self.cursor_line -= 1
                self.cursor_col = len(self.document_lines[self.cursor_line])
        elif d_col > 0:
            if self.cursor_col < len(self.document_lines[self.cursor_line]):
                self.cursor_col += 1
            elif self.cursor_line < len(self.document_lines) - 1:
                self.cursor_line += 1
                self.cursor_col = 0
        self._keep_cursor_visible()

    def wp_save(self) -> bool:
        if not self.current_document:
            self.current_document = f"document_{datetime.now():%Y%m%d_%H%M%S}.txt"
        try:
            self.document_dir.mkdir(parents=True, exist_ok=True)
            (self.document_dir / self.current_document).write_text("\n".join(self.document_lines), encoding="utf-8")
            return True
        except OSError as e:
            self.logger.error(f"Could not save document: {e}")
            return False

    def wp_new(self):
        self.document_lines = [""]
        self.cursor_line = self.cursor_col = self.wp_scroll = 0
        self.current_document = None

    def word_count(self) -> int:
        return len(" ".join(self.document_lines).split())

    # ---- Input ----

    def handle_key(self, key_code: int, char: Optional[str], modifiers: dict):
        if ecodes is None:
            return
        e = ecodes
        if self.mode == self.MODE_TERMINAL:
            if key_code == e.KEY_ENTER:
                self.terminal_execute()
            elif key_code == e.KEY_BACKSPACE:
                self.terminal_input = self.terminal_input[:-1]
            elif key_code == e.KEY_UP:
                self.terminal_scroll = max(0, self.terminal_scroll - 1)
            elif key_code == e.KEY_DOWN:
                self.terminal_scroll = min(max(0, len(self._terminal_lines()) - self.visible_lines),
                                           self.terminal_scroll + 1)
            elif char and char.isprintable():
                self.terminal_input += char
            return
        if modifiers.get("ctrl") and key_code == e.KEY_S:
            self.wp_save()
        elif modifiers.get("ctrl") and key_code == e.KEY_N:
            self.wp_new()
        elif key_code == e.KEY_ENTER:
            self.wp_enter()
        elif key_code == e.KEY_BACKSPACE:
            self.wp_backspace()
        elif key_code in (e.KEY_UP, e.KEY_DOWN):
            self.wp_move(-1 if key_code == e.KEY_UP else 1, 0)
        elif key_code in (e.KEY_LEFT, e.KEY_RIGHT):
            self.wp_move(0, -1 if key_code == e.KEY_LEFT else 1)
        elif char == "\t":
            self.wp_type("    ")
        elif char and char.isprintable():
            self.wp_type(char)

    # ---- Drawing ----

    def render(self) -> Image.Image:
        image = blank(self.width, self.height)
        draw = ImageDraw.Draw(image)
        tab_font = font(20, bold=True)
        for index, label in enumerate(("Terminal", "Writer")):
            x = 20 + index * 160
            active = self.mode == index
            draw.rectangle([x, 8, x + 150, 43], outline=BLACK, width=3 if active else 1)
            draw.text((x + (150 - draw.textlength(label, font=tab_font)) // 2, 15), label, font=tab_font, fill=BLACK)
        draw_battery(draw, self.width - 10, 12, self.battery)
        draw.line([(0, self.HEADER - 1), (self.width, self.HEADER - 1)], fill=BLACK, width=1)

        y = self.HEADER + 5
        if self.mode == self.MODE_TERMINAL:
            lines = self._terminal_lines()
            for kind, text in lines[self.terminal_scroll:self.terminal_scroll + self.visible_lines]:
                draw.text((self.MARGIN, y), text, font=self.mono_bold if kind == "prompt" else self.mono, fill=BLACK)
                y += self.char_height
            help_text = "Alt: switch tab  |  Enter: run  |  Esc: menu"
        else:
            for i in range(self.wp_scroll, min(self.wp_scroll + self.visible_lines, len(self.document_lines))):
                line = self.document_lines[i]
                if i == self.cursor_line:
                    line = line[:self.cursor_col] + "|" + line[self.cursor_col:]
                draw.text((self.MARGIN, y), line, font=self.mono, fill=BLACK)
                y += self.char_height
            if self.current_document:
                label = f"[{self.current_document}]"
                draw.text((self.width - self.MARGIN - draw.textlength(label, font=font(13)), self.HEADER + 5),
                          label, font=font(13), fill=BLACK)
            help_text = f"Alt: tab  |  Ctrl+S: save  |  {self.word_count()} words  |  Esc: menu"
        footer = self.height - self.FOOTER
        draw.line([(0, footer), (self.width, footer)], fill=BLACK, width=1)
        draw.text((self.MARGIN, footer + 7), help_text, font=font(14), fill=BLACK)
        return image
