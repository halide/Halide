from __future__ import annotations

import dataclasses
import json
import shutil
import subprocess
import tempfile
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path
from typing import Literal, Protocol

from gepa_scheduling.candidate import write_candidate
from gepa_scheduling.config import HarnessConfig
from gepa_scheduling.models import RolloutResult, TaskSpec, ValidationResult
from gepa_scheduling.scoring import score_rollout
from gepa_scheduling.validators import validate_task

SkillMode = Literal["candidate", "current", "none"]


class AgentBackend(Protocol):
    def run_agent(self, prompt: str, cwd: Path) -> RolloutResult: ...


@dataclasses.dataclass(frozen=True)
class Evaluation:
    task_id: str
    mode: SkillMode
    score: float
    rollout: RolloutResult
    validation: ValidationResult

    def as_dict(self) -> dict:
        return {
            "task_id": self.task_id,
            "mode": self.mode,
            "score": self.score,
            "rollout": dataclasses.asdict(self.rollout),
            "validation": self.validation.side_info(),
        }


class TaskRunner:
    def __init__(self, config: HarnessConfig, backend: AgentBackend) -> None:
        self.config = config
        self.backend = backend

    def evaluate(
        self,
        task: TaskSpec,
        *,
        candidate: dict[str, str] | None,
        mode: SkillMode = "candidate",
    ) -> Evaluation:
        with self._workspace(task, candidate=candidate, mode=mode) as workspace:
            prompt = _task_prompt(task)
            rollout = self.backend.run_agent(prompt, workspace)
            validation = validate_task(
                task,
                workspace,
                halide_dir=self.config.halide_dir,
                timeout_seconds=self.config.rollout_timeout_seconds,
            )
            return Evaluation(
                task_id=task.id,
                mode=mode,
                score=score_rollout(
                    rollout,
                    validation,
                    time_budget_seconds=float(self.config.rollout_timeout_seconds),
                ),
                rollout=rollout,
                validation=validation,
            )

    @contextmanager
    def _workspace(
        self,
        task: TaskSpec,
        *,
        candidate: dict[str, str] | None,
        mode: SkillMode,
    ) -> Iterator[Path]:
        self.config.run_dir.mkdir(parents=True, exist_ok=True)
        workspace = Path(tempfile.mkdtemp(prefix=f"{task.id}-", dir=self.config.run_dir))
        added = False
        try:
            subprocess.run(
                ["git", "worktree", "add", "--detach", str(workspace), "HEAD"],
                cwd=self.config.repository,
                check=True,
                capture_output=True,
                text=True,
            )
            added = True
            git_pointer = workspace / ".git"
            if git_pointer.is_file():
                git_pointer.unlink()
            harness_copy = workspace / "tools/gepa_scheduling"
            if harness_copy.exists():
                shutil.rmtree(harness_copy)
            skill_dir = workspace / ".claude/skills/scheduling"
            if mode == "none":
                if skill_dir.exists():
                    shutil.rmtree(skill_dir)
            elif mode == "candidate":
                if candidate is None:
                    raise ValueError("candidate mode requires candidate text")
                write_candidate(candidate, workspace)
            elif candidate is not None:
                write_candidate(candidate, workspace)
            if task.fixture is not None:
                shutil.copytree(task.fixture, workspace / "gepa_task")
            else:
                (workspace / "gepa_task").mkdir()
            yield workspace
        finally:
            if added:
                removal = subprocess.run(
                    ["git", "worktree", "remove", "--force", str(workspace)],
                    cwd=self.config.repository,
                    check=False,
                    capture_output=True,
                    text=True,
                )
                if removal.returncode != 0:
                    subprocess.run(
                        ["git", "worktree", "prune"],
                        cwd=self.config.repository,
                        check=False,
                        capture_output=True,
                        text=True,
                    )
            if workspace.exists():
                shutil.rmtree(workspace)


def save_evaluations(path: Path, evaluations: list[Evaluation], metadata: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    value = {
        "metadata": metadata,
        "evaluations": [evaluation.as_dict() for evaluation in evaluations],
        "mean_score": sum(item.score for item in evaluations) / len(evaluations) if evaluations else 0.0,
    }
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def _task_prompt(task: TaskSpec) -> str:
    output = (
        f"Write your final structured answer to gepa_task/{task.answer_file}."
        if task.answer_file
        else "Modify only files under gepa_task/ to solve the task."
    )
    return f"""Use the Halide scheduling skill for this task.

{task.prompt}

{output}
Do not modify tests, build artifacts, or files outside gepa_task/. Do not use the network.
Complete the task without asking questions.
"""
