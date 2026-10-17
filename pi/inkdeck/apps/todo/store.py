"""To-do list storage in data/todos.json, shared by the screen and the web API."""

import threading
import uuid
from datetime import datetime
from typing import List, Optional

from ...paths import data_path, load_json, save_json


class TodoStore:
    def __init__(self, path=None):
        self.path = path or data_path("todos.json")
        self._lock = threading.Lock()

    def _load(self) -> List[dict]:
        data = load_json(self.path, {"tasks": []})
        tasks = data.get("tasks", []) if isinstance(data, dict) else data if isinstance(data, list) else []
        missing = [task for task in tasks if "id" not in task]
        for task in missing:  # Tasks added on the device by older versions had no id
            task["id"] = str(uuid.uuid4())
            task.setdefault("completed", False)
        if missing:
            self._save(tasks)
        return tasks

    def _save(self, tasks: List[dict]):
        save_json(self.path, {"tasks": tasks})

    def tasks(self) -> List[dict]:
        with self._lock:
            return self._load()

    def add(self, text: str) -> dict:
        with self._lock:
            tasks = self._load()
            task = {"id": str(uuid.uuid4()), "text": text, "completed": False,
                    "created_at": datetime.now().isoformat(timespec="seconds")}
            tasks.append(task)
            self._save(tasks)
            return task

    def _change(self, task_id: str, change) -> Optional[dict]:
        with self._lock:
            tasks = self._load()
            for task in tasks:
                if task["id"] == task_id:
                    change(task)
                    self._save(tasks)
                    return task
            return None

    def toggle(self, task_id: str) -> Optional[dict]:
        return self._change(task_id, lambda t: t.update(completed=not t["completed"]))

    def edit(self, task_id: str, text: str) -> Optional[dict]:
        return self._change(task_id, lambda t: t.update(text=text))

    def delete(self, task_id: str) -> bool:
        with self._lock:
            tasks = self._load()
            kept = [t for t in tasks if t["id"] != task_id]
            if len(kept) == len(tasks):
                return False
            self._save(kept)
            return True
