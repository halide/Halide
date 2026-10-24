from __future__ import annotations

import json
import tempfile
from pathlib import Path
from typing import Any

from gepa.optimize_anything import EngineConfig, GEPAConfig, ReflectionConfig, optimize_anything

from gepa_scheduling.candidate import load_seed_candidate, validate_candidate
from gepa_scheduling.config import HarnessConfig
from gepa_scheduling.copilot import CopilotBackend
from gepa_scheduling.runner import TaskRunner
from gepa_scheduling.tasks import load_tasks

OBJECTIVE = """Improve Halide's scheduling skill so a coding agent produces correct, target-aware,
machine-verifiable schedules and diagnoses scheduling problems efficiently. Preserve factual
correctness above brevity or speed. Avoid task-specific memorization."""

BACKGROUND = """The candidate is a dictionary of Markdown files from one Claude-compatible skill.
Only the supplied components may change. Other guide chapters remain available and immutable.
Halide schedules may change execution order, recomputation, and temporary storage, but never
algorithm results. For loops in Halide IR use inclusive min/max. compute_at and store_at are
independent. Parallel storage must not introduce races. Prefer natural vector widths and verify
schedule shape with print_loop_nest. Keep links, YAML frontmatter, Markdown, and code fences valid."""


class CopilotReflectionLM:
    def __init__(self, backend: CopilotBackend, working_directory: Path) -> None:
        self.backend = backend
        self.working_directory = working_directory

    def __call__(self, prompt: str | list[dict[str, Any]]) -> str:
        return self.backend.complete(prompt, self.working_directory)


def run_optimization(config: HarnessConfig):
    tasks = load_tasks(config.task_manifest)
    train = [_public_example(task) for task in tasks if task.split == "train"]
    validation = [_public_example(task) for task in tasks if task.split == "validation"]
    seed = load_seed_candidate(config.repository)
    integrity = validate_candidate(seed, config.repository)
    if integrity:
        raise ValueError("seed candidate is invalid:\n" + "\n".join(integrity))

    rollout_backend = CopilotBackend(
        config.copilot_executable,
        config.copilot_model,
        timeout_seconds=config.rollout_timeout_seconds,
        max_output_bytes=config.max_output_bytes,
    )
    runner = TaskRunner(config, rollout_backend)
    task_by_id = {task.id: task for task in tasks}

    def evaluator(candidate: dict[str, str], example: dict[str, Any]):
        task = task_by_id[example["id"]]
        integrity_errors = validate_candidate(candidate, config.repository)
        if integrity_errors:
            return 0.0, {"candidate_integrity": integrity_errors}
        evaluation = runner.evaluate(task, candidate=candidate)
        side_info = evaluation.validation.side_info()
        side_info["rollout"] = {
            "error": evaluation.rollout.error,
            "returncode": evaluation.rollout.returncode,
            "stderr": evaluation.rollout.stderr[-4000:],
            "duration_seconds": evaluation.rollout.duration_seconds,
            "input_tokens": evaluation.rollout.input_tokens,
            "output_tokens": evaluation.rollout.output_tokens,
        }
        return evaluation.score, side_info

    run_dir = config.run_dir / "gepa"
    run_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="reflection-") as reflection_directory:
        reflection_backend = CopilotBackend(
            config.copilot_executable,
            config.reflection_model,
            timeout_seconds=config.reflection_timeout_seconds,
            max_output_bytes=config.max_output_bytes,
        )
        result = optimize_anything(
            seed_candidate=seed,
            evaluator=evaluator,
            dataset=train,
            valset=validation,
            objective=OBJECTIVE,
            background=BACKGROUND,
            config=GEPAConfig(
                engine=EngineConfig(
                    run_dir=str(run_dir),
                    max_metric_calls=config.max_metric_calls,
                    max_workers=config.max_workers,
                    parallel=config.max_workers > 1,
                    cache_evaluation=True,
                    write_agent_state=True,
                ),
                reflection=ReflectionConfig(
                    reflection_lm=CopilotReflectionLM(reflection_backend, Path(reflection_directory)),
                    module_selector="round_robin",
                ),
            ),
        )
    best_path = run_dir / "best_candidate.json"
    best_path.write_text(json.dumps(result.best_candidate, indent=2, sort_keys=True) + "\n")
    return result


def _public_example(task) -> dict[str, Any]:
    return {
        "id": task.id,
        "kind": task.kind,
        "prompt": task.prompt,
        "tags": list(task.tags),
    }
