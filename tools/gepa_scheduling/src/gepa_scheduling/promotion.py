from __future__ import annotations

import difflib
import hashlib
import json
from pathlib import Path
from typing import Any

from gepa_scheduling.candidate import MUTABLE_COMPONENTS, load_seed_candidate, validate_candidate
from gepa_scheduling.config import HarnessConfig
from gepa_scheduling.runner import Evaluation, TaskRunner
from gepa_scheduling.tasks import load_tasks


def candidate_hash(candidate: dict[str, str]) -> str:
    serialized = json.dumps(candidate, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(serialized).hexdigest()


def compare_candidate(
    config: HarnessConfig,
    runner: TaskRunner,
    candidate: dict[str, str],
    *,
    repeats: int,
) -> dict[str, Any]:
    integrity_errors = validate_candidate(candidate, config.repository)
    if integrity_errors:
        raise ValueError("candidate integrity failed:\n" + "\n".join(integrity_errors))
    if repeats < 1:
        raise ValueError("repeats must be positive")

    tasks = [task for task in load_tasks(config.task_manifest) if task.split == "test"]
    current_candidate = load_seed_candidate(config.repository)
    current_results: list[Evaluation] = []
    candidate_results: list[Evaluation] = []
    for _ in range(repeats):
        for task in tasks:
            current_results.append(runner.evaluate(task, candidate=current_candidate, mode="current"))
            candidate_results.append(runner.evaluate(task, candidate=candidate, mode="candidate"))

    regressions = []
    for current, proposed in zip(current_results, candidate_results, strict=True):
        if current.validation.hard_pass and not proposed.validation.hard_pass:
            regressions.append(current.task_id)
    current_correctness = _mean(item.validation.pass_fraction for item in current_results)
    candidate_correctness = _mean(item.validation.pass_fraction for item in candidate_results)
    current_score = _mean(item.score for item in current_results)
    candidate_score = _mean(item.score for item in candidate_results)
    eligible = not regressions and candidate_correctness >= current_correctness and candidate_score >= current_score
    return {
        "schema_version": 1,
        "candidate_sha256": candidate_hash(candidate),
        "repeats": repeats,
        "promotion_eligible": eligible,
        "regressed_hard_checks": sorted(set(regressions)),
        "current": {
            "mean_correctness": current_correctness,
            "mean_score": current_score,
            "evaluations": [item.as_dict() for item in current_results],
        },
        "candidate": {
            "mean_correctness": candidate_correctness,
            "mean_score": candidate_score,
            "evaluations": [item.as_dict() for item in candidate_results],
        },
        "metadata": config.versions(),
    }


def render_candidate_patch(candidate: dict[str, str], repository: Path) -> str:
    errors = validate_candidate(candidate, repository)
    if errors:
        raise ValueError("candidate integrity failed:\n" + "\n".join(errors))
    chunks = []
    for component in MUTABLE_COMPONENTS:
        original = (repository / component).read_text().splitlines(keepends=True)
        proposed = candidate[component].splitlines(keepends=True)
        chunks.extend(
            difflib.unified_diff(
                original,
                proposed,
                fromfile=f"a/{component}",
                tofile=f"b/{component}",
            )
        )
    return "".join(chunks)


def verify_promotion_report(candidate: dict[str, str], report: dict[str, Any]) -> None:
    if report.get("schema_version") != 1:
        raise ValueError("unsupported comparison report schema")
    if report.get("candidate_sha256") != candidate_hash(candidate):
        raise ValueError("comparison report does not match candidate")
    if not report.get("promotion_eligible"):
        raise ValueError("candidate did not pass promotion gates")


def _mean(values) -> float:
    values = list(values)
    return sum(values) / len(values) if values else 0.0
