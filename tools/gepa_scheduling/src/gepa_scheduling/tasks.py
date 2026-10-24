from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from gepa_scheduling.models import CommandCheck, TaskSpec


def load_tasks(manifest: Path) -> list[TaskSpec]:
    manifest = manifest.resolve()
    raw = json.loads(manifest.read_text())
    if raw.get("schema_version") != 1:
        raise ValueError("unsupported task manifest schema")
    tasks = [_parse_task(item, manifest.parent) for item in raw["tasks"]]
    ids = [task.id for task in tasks]
    if len(ids) != len(set(ids)):
        raise ValueError("task IDs must be unique")
    for split in ("train", "validation", "test"):
        if not any(task.split == split for task in tasks):
            raise ValueError(f"task manifest has no {split} tasks")
    return tasks


def _parse_task(value: dict[str, Any], root: Path) -> TaskSpec:
    split = value["split"]
    kind = value["kind"]
    if split not in {"train", "validation", "test"}:
        raise ValueError(f"invalid split for {value.get('id')}: {split}")
    if kind not in {"code", "diagnosis"}:
        raise ValueError(f"invalid kind for {value.get('id')}: {kind}")
    fixture = _optional_path(root, value.get("fixture"))
    answer_file = Path(value["answer_file"]) if value.get("answer_file") else None
    if fixture is not None and not fixture.exists():
        raise ValueError(f"fixture does not exist for {value['id']}: {fixture}")
    if kind == "diagnosis" and answer_file is None:
        raise ValueError(f"diagnosis task {value['id']} requires answer_file")
    if kind == "code" and not value.get("commands"):
        raise ValueError(f"code task {value['id']} requires command checks")
    return TaskSpec(
        id=value["id"],
        split=split,
        kind=kind,
        prompt=value["prompt"],
        fixture=fixture,
        answer_file=answer_file,
        expected_json=value.get("expected_json", {}),
        commands=tuple(CommandCheck.from_dict(command) for command in value.get("commands", ())),
        tags=tuple(value.get("tags", ())),
    )


def _optional_path(root: Path, value: str | None) -> Path | None:
    return (root / value).resolve() if value else None
