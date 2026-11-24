import json
from pathlib import Path

from gepa_scheduling.models import TaskSpec
from gepa_scheduling.validators import validate_task


def test_diagnosis_validator_checks_nested_values(tmp_path: Path) -> None:
    task_dir = tmp_path / "gepa_task"
    task_dir.mkdir()
    (task_dir / "answer.json").write_text(json.dumps({"schedule": {"axis": "x"}}))
    task = TaskSpec(
        id="diagnosis",
        split="train",
        kind="diagnosis",
        prompt="",
        fixture=None,
        answer_file=Path("answer.json"),
        expected_json={"schedule.axis": "x"},
        commands=(),
        tags=(),
    )
    result = validate_task(task, tmp_path, halide_dir=tmp_path, timeout_seconds=1)
    assert result.hard_pass
    assert result.pass_fraction == 1.0
