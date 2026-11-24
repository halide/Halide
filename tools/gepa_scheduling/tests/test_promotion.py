import json
from pathlib import Path

import pytest

from gepa_scheduling.candidate import MUTABLE_COMPONENTS, load_seed_candidate
from gepa_scheduling.promotion import (
    candidate_hash,
    render_candidate_patch,
    verify_promotion_report,
)

REPOSITORY = Path(__file__).resolve().parents[3]


def test_candidate_patch_is_reviewable_and_does_not_write_files() -> None:
    candidate = load_seed_candidate(REPOSITORY)
    component = MUTABLE_COMPONENTS[-1]
    original = candidate[component]
    candidate[component] += "\n<!-- candidate marker -->\n"
    patch = render_candidate_patch(candidate, REPOSITORY)
    assert f"a/{component}" in patch
    assert "+<!-- candidate marker -->" in patch
    assert (REPOSITORY / component).read_text() == original


def test_report_must_match_candidate_and_pass() -> None:
    candidate = load_seed_candidate(REPOSITORY)
    report = {
        "schema_version": 1,
        "candidate_sha256": candidate_hash(candidate),
        "promotion_eligible": True,
    }
    verify_promotion_report(candidate, json.loads(json.dumps(report)))
    report["candidate_sha256"] = "wrong"
    with pytest.raises(ValueError, match="does not match"):
        verify_promotion_report(candidate, report)
