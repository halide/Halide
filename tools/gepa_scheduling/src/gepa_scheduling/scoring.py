from __future__ import annotations

from gepa_scheduling.models import RolloutResult, ValidationResult


def score_rollout(
    rollout: RolloutResult,
    validation: ValidationResult,
    *,
    token_budget: int = 40_000,
    time_budget_seconds: float = 900.0,
) -> float:
    if rollout.error or rollout.returncode != 0:
        return 0.0

    correctness = validation.pass_fraction
    if not validation.hard_pass:
        return min(0.49, 0.49 * correctness)

    token_total = None
    if rollout.input_tokens is not None and rollout.output_tokens is not None:
        token_total = rollout.input_tokens + rollout.output_tokens
    token_efficiency = 0.5 if token_total is None else max(0.0, 1.0 - token_total / token_budget)
    time_efficiency = max(0.0, 1.0 - rollout.duration_seconds / time_budget_seconds)
    efficiency = (token_efficiency + time_efficiency) / 2
    return min(1.0, 0.9 + 0.05 * correctness + 0.05 * efficiency)
