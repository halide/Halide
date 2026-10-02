from __future__ import annotations

import dataclasses
from pathlib import Path
from typing import Any, Literal

Split = Literal["train", "validation", "test"]
TaskKind = Literal["code", "diagnosis"]


@dataclasses.dataclass(frozen=True)
class CommandCheck:
    argv: tuple[str, ...]
    required_patterns: tuple[str, ...] = ()
    forbidden_patterns: tuple[str, ...] = ()
    hard: bool = True

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> CommandCheck:
        return cls(
            argv=tuple(value["argv"]),
            required_patterns=tuple(value.get("required_patterns", ())),
            forbidden_patterns=tuple(value.get("forbidden_patterns", ())),
            hard=bool(value.get("hard", True)),
        )


@dataclasses.dataclass(frozen=True)
class TaskSpec:
    id: str
    split: Split
    kind: TaskKind
    prompt: str
    fixture: Path | None
    answer_file: Path | None
    expected_json: dict[str, Any]
    commands: tuple[CommandCheck, ...]
    tags: tuple[str, ...]


@dataclasses.dataclass(frozen=True)
class RolloutResult:
    returncode: int
    stdout: str
    stderr: str
    duration_seconds: float
    input_tokens: int | None = None
    output_tokens: int | None = None
    error: str | None = None


@dataclasses.dataclass(frozen=True)
class CheckResult:
    name: str
    passed: bool
    hard: bool
    feedback: str


@dataclasses.dataclass(frozen=True)
class ValidationResult:
    checks: tuple[CheckResult, ...]

    @property
    def hard_pass(self) -> bool:
        return all(check.passed for check in self.checks if check.hard)

    @property
    def pass_fraction(self) -> float:
        if not self.checks:
            return 0.0
        return sum(check.passed for check in self.checks) / len(self.checks)

    def side_info(self) -> dict[str, Any]:
        return {
            "hard_pass": self.hard_pass,
            "checks": [dataclasses.asdict(check) for check in self.checks],
        }
