from pathlib import Path

from gepa_scheduling.candidate import MUTABLE_COMPONENTS, load_seed_candidate, validate_candidate

REPOSITORY = Path(__file__).resolve().parents[3]


def test_current_candidate_is_valid() -> None:
    candidate = load_seed_candidate(REPOSITORY)
    assert set(candidate) == set(MUTABLE_COMPONENTS)
    assert validate_candidate(candidate, REPOSITORY) == []


def test_manifest_name_cannot_change() -> None:
    candidate = load_seed_candidate(REPOSITORY)
    candidate[MUTABLE_COMPONENTS[0]] = candidate[MUTABLE_COMPONENTS[0]].replace(
        "name: scheduling",
        "name: other",
        1,
    )
    assert "frontmatter name" in "\n".join(validate_candidate(candidate, REPOSITORY))


def test_component_set_is_exact() -> None:
    candidate = load_seed_candidate(REPOSITORY)
    candidate.pop(MUTABLE_COMPONENTS[-1])
    assert "component mismatch" in "\n".join(validate_candidate(candidate, REPOSITORY))
