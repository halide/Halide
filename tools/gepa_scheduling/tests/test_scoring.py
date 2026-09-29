from gepa_scheduling.models import CheckResult, RolloutResult, ValidationResult
from gepa_scheduling.scoring import score_rollout


def _rollout(**overrides) -> RolloutResult:
    values = {
        "returncode": 0,
        "stdout": "",
        "stderr": "",
        "duration_seconds": 10.0,
        "input_tokens": 100,
        "output_tokens": 50,
    }
    values.update(overrides)
    return RolloutResult(**values)


def test_hard_failure_is_capped_below_passing_score() -> None:
    failed = ValidationResult((CheckResult("hard", False, True, "failed"),))
    passed = ValidationResult((CheckResult("hard", True, True, "passed"),))
    assert score_rollout(_rollout(), failed) <= 0.49
    assert score_rollout(_rollout(), passed) >= 0.9


def test_agent_failure_scores_zero() -> None:
    passed = ValidationResult((CheckResult("hard", True, True, "passed"),))
    assert score_rollout(_rollout(returncode=1, error="failed"), passed) == 0.0
