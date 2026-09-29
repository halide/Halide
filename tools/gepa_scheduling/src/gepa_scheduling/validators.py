from __future__ import annotations

import json
import re
import subprocess
from pathlib import Path
from typing import Any

from gepa_scheduling.models import CheckResult, TaskSpec, ValidationResult


def validate_task(task: TaskSpec, workspace: Path, *, halide_dir: Path, timeout_seconds: int) -> ValidationResult:
    if task.kind == "diagnosis":
        return _validate_diagnosis(task, workspace)
    return _validate_commands(task, workspace, halide_dir=halide_dir, timeout_seconds=timeout_seconds)


def _validate_diagnosis(task: TaskSpec, workspace: Path) -> ValidationResult:
    assert task.answer_file is not None
    answer_path = workspace / "gepa_task" / task.answer_file
    if not answer_path.is_file():
        return ValidationResult((CheckResult("answer-file", False, True, f"missing {task.answer_file}"),))
    try:
        actual = json.loads(answer_path.read_text())
    except json.JSONDecodeError as error:
        return ValidationResult((CheckResult("answer-json", False, True, f"invalid JSON: {error}"),))

    checks = []
    for dotted_key, expected in task.expected_json.items():
        found, actual_value = _lookup(actual, dotted_key)
        passed = found and actual_value == expected
        checks.append(
            CheckResult(
                name=f"answer:{dotted_key}",
                passed=passed,
                hard=True,
                feedback=f"expected {expected!r}; got {actual_value!r}" if not passed else "matched",
            )
        )
    return ValidationResult(tuple(checks))


def _validate_commands(
    task: TaskSpec,
    workspace: Path,
    *,
    halide_dir: Path,
    timeout_seconds: int,
) -> ValidationResult:
    task_dir = workspace / "gepa_task"
    checks: list[CheckResult] = []
    substitutions = {
        "halide_dir": str(halide_dir),
        "task_dir": str(task_dir),
    }
    for index, command in enumerate(task.commands):
        argv = [argument.format(**substitutions) for argument in command.argv]
        try:
            result = subprocess.run(
                argv,
                cwd=task_dir,
                check=False,
                capture_output=True,
                text=True,
                timeout=timeout_seconds,
            )
            output = result.stdout + "\n" + result.stderr
            failures = []
            if result.returncode != 0:
                failures.append(f"exit code {result.returncode}")
            failures.extend(
                f"missing /{pattern}/" for pattern in command.required_patterns if re.search(pattern, output) is None
            )
            failures.extend(
                f"forbidden /{pattern}/" for pattern in command.forbidden_patterns if re.search(pattern, output)
            )
            excerpt = output[-4000:]
            feedback = "; ".join(failures) + (f"\n{excerpt}" if failures else "")
            checks.append(CheckResult(f"command:{index}", not failures, command.hard, feedback or "passed"))
        except (OSError, subprocess.TimeoutExpired) as error:
            checks.append(CheckResult(f"command:{index}", False, command.hard, str(error)))
    return ValidationResult(tuple(checks))


def _lookup(value: dict[str, Any], dotted_key: str) -> tuple[bool, Any]:
    current: Any = value
    for part in dotted_key.split("."):
        if not isinstance(current, dict) or part not in current:
            return False, None
        current = current[part]
    return True, current
