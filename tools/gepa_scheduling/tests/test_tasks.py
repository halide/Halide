from pathlib import Path

from gepa_scheduling.tasks import load_tasks

ROOT = Path(__file__).resolve().parents[1]


def test_manifest_has_all_splits_and_task_kinds() -> None:
    tasks = load_tasks(ROOT / "tasks/tasks.json")
    assert len(tasks) == 12
    assert {task.split for task in tasks} == {"train", "validation", "test"}
    assert {task.kind for task in tasks} == {"code", "diagnosis"}
    assert len({task.id for task in tasks}) == len(tasks)
