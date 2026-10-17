"""To-do list on the e-paper screen. Tasks are added from the web interface."""

from PIL import Image, ImageDraw

from ...ui.common import BLACK, blank, draw_centered, draw_hint, draw_title_bar, font, wrap_text
from .store import TodoStore


class ToDoScreen:
    LINE = 36
    MAX_LINES = 3

    def __init__(self, width: int, height: int, store: TodoStore, battery=None):
        self.width, self.height = width, height
        self.store = store
        self.battery = battery
        self.current_index = 0

    def _tasks(self):
        tasks = self.store.tasks()
        self.current_index = min(self.current_index, max(0, len(tasks) - 1))
        return tasks

    def next_item(self):
        tasks = self._tasks()
        if tasks:
            self.current_index = (self.current_index + 1) % len(tasks)

    def prev_item(self):
        tasks = self._tasks()
        if tasks:
            self.current_index = (self.current_index - 1) % len(tasks)

    def toggle_todo(self):
        tasks = self._tasks()
        if tasks:
            self.store.toggle(tasks[self.current_index]["id"])

    def render(self) -> Image.Image:
        image = blank(self.width, self.height)
        draw = ImageDraw.Draw(image)
        draw_title_bar(draw, self.width, "To Do", self.battery)
        draw_hint(draw, self.width, self.height, "Press: next task   |   Hold: menu")
        tasks = self._tasks()
        item_font = font(26)
        if not tasks:
            draw_centered(draw, self.height // 2 - 40, "No tasks yet.", item_font, self.width)
            draw_centered(draw, self.height // 2, "Add them from the web interface.", font(18), self.width)
            return image

        box, text_x = 24, 70
        bottom = self.height - 60
        # Page through the list so the selected task is always visible.
        layouts = [wrap_text(draw, t["text"], item_font, self.width - text_x - 28, self.MAX_LINES) for t in tasks]
        heights = [len(lines) * self.LINE + 14 for lines in layouts]
        start, used = 0, 0
        for i in range(self.current_index + 1):
            if used + heights[i] > bottom - 80:
                start, used = i, 0
            used += heights[i]

        y = 80
        for i in range(start, len(tasks)):
            if y + heights[i] > bottom:
                break
            task, lines = tasks[i], layouts[i]
            if i == self.current_index:
                draw.rounded_rectangle([16, y - 4, self.width - 16, y + heights[i] - 8], radius=8,
                                       outline=BLACK, width=3)
            cy = y + 4
            draw.rectangle([box, cy, box + 26, cy + 26], outline=BLACK, width=2)
            if task["completed"]:
                draw.line([(box + 5, cy + 5), (box + 21, cy + 21)], fill=BLACK, width=3)
                draw.line([(box + 21, cy + 5), (box + 5, cy + 21)], fill=BLACK, width=3)
            ty = y
            for line in lines:
                draw.text((text_x, ty), line, font=item_font, fill=BLACK)
                if task["completed"]:
                    mid = ty + 17
                    draw.line([(text_x, mid), (text_x + draw.textlength(line, font=item_font), mid)],
                              fill=BLACK, width=3)
                ty += self.LINE
            y += heights[i]
        return image
