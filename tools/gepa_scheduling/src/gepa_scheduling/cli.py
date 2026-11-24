from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from gepa_scheduling.candidate import load_seed_candidate, validate_candidate
from gepa_scheduling.config import HarnessConfig
from gepa_scheduling.copilot import CopilotBackend, FakeCopilotBackend
from gepa_scheduling.optimize import run_optimization
from gepa_scheduling.promotion import compare_candidate, render_candidate_patch, verify_promotion_report
from gepa_scheduling.runner import TaskRunner, save_evaluations
from gepa_scheduling.tasks import load_tasks


def main(argv: list[str] | None = None) -> int:
    parser = _parser()
    args = parser.parse_args(argv)
    try:
        return args.handler(args)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Optimize Halide's scheduling skill with GEPA")
    parser.add_argument("--config", type=Path, default=Path("config.toml"))
    subparsers = parser.add_subparsers(required=True)

    validate = subparsers.add_parser("validate", help="Validate configuration, tasks, and the current candidate")
    validate.set_defaults(handler=_validate)

    smoke = subparsers.add_parser("smoke", help="Run a zero-credit fake-backend smoke test")
    smoke.set_defaults(handler=_smoke)

    evaluate = subparsers.add_parser("evaluate", help="Evaluate current, absent, or saved candidate skill")
    evaluate.add_argument("--split", choices=("train", "validation", "test"), default="test")
    evaluate.add_argument("--mode", choices=("current", "none", "candidate"), default="current")
    evaluate.add_argument("--candidate", type=Path)
    evaluate.add_argument("--output", type=Path, required=True)
    evaluate.set_defaults(handler=_evaluate)

    optimize = subparsers.add_parser("optimize", help="Run the bounded GEPA experiment")
    optimize.set_defaults(handler=_optimize)

    compare = subparsers.add_parser("compare", help="Run paired held-out promotion checks")
    compare.add_argument("--candidate", type=Path, required=True)
    compare.add_argument("--repeats", type=int, default=3)
    compare.add_argument("--output", type=Path, required=True)
    compare.set_defaults(handler=_compare)

    stage = subparsers.add_parser("stage", help="Write a patch for a candidate that passed comparison")
    stage.add_argument("--candidate", type=Path, required=True)
    stage.add_argument("--report", type=Path, required=True)
    stage.add_argument("--output", type=Path, required=True)
    stage.set_defaults(handler=_stage)
    return parser


def _validate(args: argparse.Namespace) -> int:
    config = HarnessConfig.load(args.config)
    tasks = load_tasks(config.task_manifest)
    errors = validate_candidate(load_seed_candidate(config.repository), config.repository)
    if errors:
        raise ValueError("\n".join(errors))
    print(f"Validated {len(tasks)} tasks and {len(load_seed_candidate(config.repository))} candidate components.")
    print(json.dumps(config.versions(), indent=2, sort_keys=True))
    return 0


def _smoke(args: argparse.Namespace) -> int:
    config = HarnessConfig.load(args.config)
    tasks = load_tasks(config.task_manifest)
    diagnosis = next(task for task in tasks if task.kind == "diagnosis")
    files = {
        f"gepa_task/{diagnosis.answer_file}": json.dumps(_undot(diagnosis.expected_json)),
    }
    runner = TaskRunner(config, FakeCopilotBackend(files, require_sealed=True))
    evaluation = runner.evaluate(diagnosis, candidate=load_seed_candidate(config.repository))
    if not evaluation.validation.hard_pass:
        raise RuntimeError(json.dumps(evaluation.as_dict(), indent=2))
    print(json.dumps(evaluation.as_dict(), indent=2, sort_keys=True))
    return 0


def _evaluate(args: argparse.Namespace) -> int:
    config = HarnessConfig.load(args.config)
    tasks = [task for task in load_tasks(config.task_manifest) if task.split == args.split]
    if args.mode == "candidate":
        if args.candidate is None:
            raise ValueError("--candidate is required for candidate mode")
        candidate = json.loads(args.candidate.read_text())
    elif args.mode == "current":
        candidate = load_seed_candidate(config.repository)
    else:
        candidate = None
    backend = CopilotBackend(
        config.copilot_executable,
        config.copilot_model,
        timeout_seconds=config.rollout_timeout_seconds,
        max_output_bytes=config.max_output_bytes,
    )
    runner = TaskRunner(config, backend)
    evaluations = [runner.evaluate(task, candidate=candidate, mode=args.mode) for task in tasks]
    save_evaluations(args.output, evaluations, config.versions())
    print(f"Wrote {len(evaluations)} evaluations to {args.output}")
    return 0


def _optimize(args: argparse.Namespace) -> int:
    config = HarnessConfig.load(args.config)
    result = run_optimization(config)
    print(f"Best validation score: {result.val_aggregate_scores[result.best_idx]:.6f}")
    print(f"Best candidate index: {result.best_idx}")
    return 0


def _compare(args: argparse.Namespace) -> int:
    config = HarnessConfig.load(args.config)
    candidate = json.loads(args.candidate.read_text())
    backend = CopilotBackend(
        config.copilot_executable,
        config.copilot_model,
        timeout_seconds=config.rollout_timeout_seconds,
        max_output_bytes=config.max_output_bytes,
    )
    report = compare_candidate(config, TaskRunner(config, backend), candidate, repeats=args.repeats)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"Wrote paired comparison to {args.output}")
    return 0 if report["promotion_eligible"] else 2


def _stage(args: argparse.Namespace) -> int:
    config = HarnessConfig.load(args.config)
    candidate = json.loads(args.candidate.read_text())
    report = json.loads(args.report.read_text())
    verify_promotion_report(candidate, report)
    patch = render_candidate_patch(candidate, config.repository)
    if not patch:
        raise ValueError("candidate is identical to the current skill")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(patch)
    print(f"Wrote reviewable patch to {args.output}; repository files were not modified")
    return 0


def _undot(values: dict[str, object]) -> dict[str, object]:
    result: dict[str, object] = {}
    for dotted_key, value in values.items():
        current = result
        parts = dotted_key.split(".")
        for part in parts[:-1]:
            child = current.setdefault(part, {})
            if not isinstance(child, dict):
                raise ValueError(f"overlapping expected JSON key: {dotted_key}")
            current = child
        current[parts[-1]] = value
    return result


if __name__ == "__main__":
    raise SystemExit(main())
