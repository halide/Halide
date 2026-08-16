"""Assembles the final static site: one HTML page per lesson (syntax-
highlighted code with captured output/figures inlined, for both the C++ and
-- where one exists -- Python source) plus an index page with a shared nav
sidebar. No Bootstrap, and the only JS is the small C++/Python source
switcher (see assets/lang-switch.js) -- collapsible output blocks are native
<details>/<summary>.
"""

from __future__ import annotations

import itertools
import shutil
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

from jinja2 import Environment, FileSystemLoader, select_autoescape
from markupsafe import Markup

from . import highlight
from .lesson import Lesson, SourceVariant
from .sitemap import SitemapEntry

_ASSETS_DIR = Path(__file__).parent / "assets"
_TEMPLATES_DIR = Path(__file__).parent / "templates"


@dataclass
class ContentBlock:
    kind: str
    source: str | None = None
    lexer: str | None = None
    text: str | None = None
    src: str | None = None
    alt: str | None = None


@dataclass
class Insertion:
    output: ContentBlock | None = None
    figures: list[ContentBlock] = field(default_factory=list)


@dataclass
class NavigationItem:
    label: str
    href: str | None = None
    children: list[NavigationItem] = field(default_factory=list)


@dataclass
class Page:
    title: str
    heading: str
    current_href: str | None
    blocks: list[ContentBlock] = field(default_factory=list)
    python_blocks: list[ContentBlock] = field(default_factory=list)


def _build_insertions(
    variant: SourceVariant,
    snippets: dict[int, str],
    output_dir: Path,
) -> dict[int, Insertion]:
    insertions: defaultdict[int, Insertion] = defaultdict(Insertion)
    for line, text in snippets.items():
        insertions[line].output = ContentBlock(kind="output", text=text)
    for line, figure_names in variant.figure_lines.items():
        for figure_name in figure_names:
            if figure := _render_figure(figure_name, output_dir):
                insertions[line].figures.append(figure)
    return insertions


def _render_figure(figure_ref: str, output_dir: Path) -> ContentBlock | None:
    # Figures are materialized into output_dir/figures once, up front, by
    # render_assets -- not here, since this runs as one of many concurrent
    # per-lesson render processes and can't safely copy into a shared
    # directory itself (concurrent "does it exist yet" checks would race).
    name = Path(figure_ref).name
    if not (output_dir / "figures" / name).exists():
        return None
    rel = f"figures/{name}"
    kind = "video" if name.lower().endswith(".mp4") else "image"
    return ContentBlock(kind=kind, src=rel, alt=name)


def _trim_blank_lines(lines: list[str]) -> list[str]:
    start, end = 0, len(lines)
    while start < end and not lines[start].strip():
        start += 1
    while end > start and not lines[end - 1].strip():
        end -= 1
    return lines[start:end]


def _highlight_chunk(lines: list[str], lexer: str) -> ContentBlock | None:
    trimmed = _trim_blank_lines(lines)
    if not trimmed:
        return None
    return ContentBlock(kind="code", source="\n".join(trimmed), lexer=lexer)


def _render_blocks(
    variant: SourceVariant, insertions: dict[int, Insertion]
) -> list[ContentBlock]:
    blocks: list[ContentBlock | None] = []
    start = 0  # 0-indexed offset into variant.display_lines
    for line_no, insertion in sorted(insertions.items()):
        blocks.append(
            _highlight_chunk(variant.display_lines[start:line_no], variant.lexer)
        )
        blocks.append(insertion.output)
        blocks.extend(insertion.figures)
        start = line_no
    blocks.append(_highlight_chunk(variant.display_lines[start:], variant.lexer))
    return [block for block in blocks if block is not None]


def _variant_blocks(
    variant: SourceVariant | None,
    snippets: dict[int, str],
    output_dir: Path,
) -> list[ContentBlock]:
    if variant is None:
        return []
    insertions = _build_insertions(variant, snippets, output_dir)
    return _render_blocks(variant, insertions)


def _navigation_items(sitemap: list[SitemapEntry]) -> list[NavigationItem]:
    return [
        _navigation_item(number, list(group))
        for number, group in itertools.groupby(sitemap, key=lambda entry: entry.number)
    ]


def _navigation_item(number: int, entries: list[SitemapEntry]) -> NavigationItem:
    if len(entries) == 1:
        entry = entries[0]
        return _lesson_item(entry, f"{number}. {entry.title}")

    # Multi-part lessons (10, 15, 16, 21) share a nav header and use each
    # title's subtitle as their indented link label.
    base_title, _ = _title_parts(entries[0].title)
    return NavigationItem(
        label=f"{number}. {base_title}",
        children=[
            _lesson_item(entry, _title_parts(entry.title)[1] or entry.title)
            for entry in entries
        ],
    )


def _lesson_item(entry: SitemapEntry, label: str) -> NavigationItem:
    return NavigationItem(label, f"{entry.slug}.html")


def _title_parts(title: str) -> tuple[str, str | None]:
    base, separator, subtitle = title.partition(":")
    subtitle = subtitle.lstrip()
    if not separator or not subtitle:
        return title, None
    return base, subtitle[:1].upper() + subtitle[1:]


def _template_environment() -> Environment:
    environment = Environment(
        loader=FileSystemLoader(_TEMPLATES_DIR),
        autoescape=select_autoescape(["html", "j2"]),
        trim_blocks=True,
        lstrip_blocks=True,
    )
    environment.filters["highlight"] = _highlight_filter
    return environment


def _highlight_filter(source: str, lexer: str) -> Markup:
    """Convert source to the trusted markup emitted by Pygments."""
    return Markup(highlight.highlight_source(source, lexer))


def render_assets(figures_dir: Path, output_dir: Path) -> None:
    """Materializes everything shared across pages that isn't specific to any
    one lesson: the stylesheet, the language-switch script, and the figures
    directory. Run once, up front, from a single process -- unlike lesson
    pages, these aren't safe to produce from several concurrent per-lesson
    render processes (concurrent writes/copies into the same shared paths
    would race)."""
    output_dir.mkdir(parents=True, exist_ok=True)
    base_css = (_ASSETS_DIR / "style.css").read_text()
    (output_dir / "style.css").write_text(
        base_css + "\n" + highlight.stylesheet() + "\n"
    )
    shutil.copyfile(_ASSETS_DIR / "lang-switch.js", output_dir / "lang-switch.js")

    dest_figures = output_dir / "figures"
    if dest_figures.exists():
        shutil.rmtree(dest_figures)
    if figures_dir.exists():
        shutil.copytree(figures_dir, dest_figures)


def render_index(sitemap: list[SitemapEntry], output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    templates = _template_environment()
    index_template = templates.get_template("index.html.j2")
    index_page = index_template.render(
        navigation=_navigation_items(sitemap),
        page=Page(title="Introduction", heading="Introduction", current_href=None),
        root="",
    )
    (output_dir / "index.html").write_text(index_page)


def render_lesson_page(
    lesson: Lesson,
    sitemap: list[SitemapEntry],
    snippets: dict[int, str],
    python_snippets: dict[int, str],
    output_dir: Path,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    templates = _template_environment()
    lesson_template = templates.get_template("code_lesson.html.j2")
    page = lesson_template.render(
        navigation=_navigation_items(sitemap),
        page=Page(
            title=f"Lesson {lesson.number}: {lesson.title}",
            heading=f"Lesson {lesson.number}: {lesson.title}",
            current_href=f"{lesson.slug}.html",
            blocks=_variant_blocks(lesson.cpp, snippets, output_dir),
            python_blocks=_variant_blocks(lesson.python, python_snippets, output_dir),
        ),
        root="",
    )
    (output_dir / f"{lesson.slug}.html").write_text(page)
