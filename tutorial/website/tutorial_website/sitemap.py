"""The cheap subset of lesson metadata (number, slug, title, language
availability) needed to render the shared navigation sidebar on every page.

Every lesson page's nav includes links to *all* lessons, but rendering one
lesson's page shouldn't require rescanning every other lesson's source file
just to learn its title -- so this is precomputed once, up front, and shared
(as sitemap.json) with each per-lesson render invocation.
"""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass
from pathlib import Path

from .lesson import discover_lessons


@dataclass(frozen=True)
class SitemapEntry:
    number: int
    slug: str
    title: str
    has_python: bool


def build_sitemap(tutorial_dir: Path) -> list[SitemapEntry]:
    # No binary manifest is needed: only number/slug/title/has_python are
    # used for navigation, none of which depend on a lesson's built binary.
    lessons = discover_lessons(tutorial_dir, manifest={})
    return [
        SitemapEntry(
            number=lesson.number,
            slug=lesson.slug,
            title=lesson.title,
            has_python=lesson.python is not None,
        )
        for lesson in lessons
    ]


def write_sitemap(entries: list[SitemapEntry], path: Path) -> None:
    content = json.dumps([asdict(entry) for entry in entries], indent=2) + "\n"
    # CMake marks this custom command "restat", which lets ninja skip
    # rebuilding every downstream per-lesson page when this output didn't
    # actually change -- but only if this file's mtime doesn't change either,
    # so the write must be skipped entirely when the content is identical.
    # Editing a lesson's code without touching its number/slug/title (by far
    # the common case) should invalidate only that one lesson's page, not
    # every other lesson's.
    if path.exists() and path.read_text() == content:
        return
    path.write_text(content)


def load_sitemap(path: Path) -> list[SitemapEntry]:
    data = json.loads(path.read_text())
    return [SitemapEntry(**entry) for entry in data]
