from __future__ import annotations

import re
from pathlib import Path, PurePosixPath
from typing import Any

import yaml

MUTABLE_COMPONENTS = (
    ".claude/skills/scheduling/SKILL.md",
    ".claude/skills/scheduling/references/directive-cheatsheet.md",
    ".claude/skills/scheduling/references/guide/05-scheduling-for-cpus.md",
    ".claude/skills/scheduling/references/guide/06-scheduling-for-gpus.md",
    ".claude/skills/scheduling/references/guide/07-what-to-schedule.md",
    ".claude/skills/scheduling/references/guide/08-benchmarking-and-profiling.md",
    ".claude/skills/scheduling/references/guide/09-reading-the-stmt-file.md",
    ".claude/skills/scheduling/references/guide/10-recipes.md",
    ".claude/skills/scheduling/references/guide/11-pitfalls.md",
)

_LINK = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")
_MAX_COMPONENT_BYTES = 128 * 1024


def load_seed_candidate(repository: Path) -> dict[str, str]:
    return {component: (repository / component).read_text() for component in MUTABLE_COMPONENTS}


def validate_candidate(candidate: dict[str, str], repository: Path) -> list[str]:
    errors: list[str] = []
    if set(candidate) != set(MUTABLE_COMPONENTS):
        missing = sorted(set(MUTABLE_COMPONENTS) - set(candidate))
        extra = sorted(set(candidate) - set(MUTABLE_COMPONENTS))
        errors.append(f"candidate component mismatch; missing={missing}, extra={extra}")
        return errors

    for component, content in candidate.items():
        if not isinstance(content, str):
            errors.append(f"{component}: content is not text")
            continue
        if len(content.encode()) > _MAX_COMPONENT_BYTES:
            errors.append(f"{component}: exceeds {_MAX_COMPONENT_BYTES} bytes")
        if content.count("```") % 2:
            errors.append(f"{component}: unbalanced fenced code blocks")
        errors.extend(_validate_links(component, content, candidate, repository))

    errors.extend(_validate_manifest(candidate[MUTABLE_COMPONENTS[0]]))
    return errors


def write_candidate(candidate: dict[str, str], root: Path) -> None:
    errors = validate_candidate(candidate, root)
    if errors:
        raise ValueError("invalid candidate:\n" + "\n".join(errors))
    for component, content in candidate.items():
        path = root / component
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)


def _validate_manifest(content: str) -> list[str]:
    if not content.startswith("---\n"):
        return ["SKILL.md: missing YAML frontmatter"]
    try:
        _, frontmatter, _ = content.split("---", 2)
        metadata: dict[str, Any] = yaml.safe_load(frontmatter)
    except (ValueError, yaml.YAMLError) as error:
        return [f"SKILL.md: invalid YAML frontmatter: {error}"]
    errors = []
    if metadata.get("name") != "scheduling":
        errors.append("SKILL.md: frontmatter name must remain 'scheduling'")
    description = metadata.get("description")
    if not isinstance(description, str) or not description.strip():
        errors.append("SKILL.md: frontmatter description must be non-empty text")
    elif len(description) > 1024:
        errors.append("SKILL.md: frontmatter description exceeds 1024 characters")
    return errors


def _validate_links(
    component: str,
    content: str,
    candidate: dict[str, str],
    repository: Path,
) -> list[str]:
    errors = []
    component_path = PurePosixPath(component)
    for raw_target in _LINK.findall(content):
        target = raw_target.split("#", 1)[0]
        if not target or "://" in target or target.startswith(("mailto:", "/")):
            continue
        normalized = str(component_path.parent.joinpath(target))
        parts: list[str] = []
        for part in PurePosixPath(normalized).parts:
            if part == "..":
                if parts:
                    parts.pop()
            elif part != ".":
                parts.append(part)
        resolved = "/".join(parts)
        if resolved not in candidate and not (repository / resolved).exists():
            errors.append(f"{component}: broken relative link {raw_target}")
    return errors
