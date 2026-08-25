"""Renders each lesson as a MyST page: syntax-highlighted code with captured
output/figures inlined. Collapsible output blocks use sphinx-design's
dropdown directive; code blocks are plain fenced blocks, so Sphinx's own
Pygments highlighting applies -- both consistent with the rest of the site,
instead of a separate hardcoded dark theme and an unstyled bare <details>.

Lessons with a Python translation (see lesson.py's `Lesson.python`) render
both variants inside a sphinx-design {tab-set}, using a shared :sync: key
(design-tabs.js persists the chosen tab across page loads via
sessionStorage) so a reader's C++/Python preference carries over as they
move between lessons. Each variant's blocks are built independently -- a
Python translation isn't expected to have the same number of interesting
lines or figures as its C++ counterpart, let alone at corresponding
positions -- so the two block lists are never assumed to line up.
"""

from __future__ import annotations

import html
import itertools
import os
import re
import shutil
import tempfile
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

from .lesson import Lesson, SourceVariant
from .sitemap import SitemapEntry


# The C++ lessons use paths such as "images/rgb.png" from tutorial/'s build
# directory; their Python translations refer to the same files relative to
# python_bindings/halide/tutorial/. Both spellings identify inputs whose
# contents can affect captured output, so include them in the lesson command's
# depfile.
_TUTORIAL_INPUT_IMAGE_RE = re.compile(
    r"(?:\.\./\.\./tutorial/)?images/([A-Za-z0-9_.-]+)"
)


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


def _build_insertions(
    variant: SourceVariant, snippets: dict[int, str], output_dir: Path
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
    # Each lesson materializes its referenced figures into output_dir/figures
    # before rendering, using atomic replacements so concurrent lesson
    # commands can safely copy the same asset.
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


def _code_block(lines: list[str], lexer: str) -> ContentBlock | None:
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
        blocks.append(_code_block(variant.display_lines[start:line_no], variant.lexer))
        blocks.append(insertion.output)
        blocks.extend(insertion.figures)
        start = line_no
    blocks.append(_code_block(variant.display_lines[start:], variant.lexer))
    return [block for block in blocks if block is not None]


def _variant_blocks(
    variant: SourceVariant, snippets: dict[int, str], output_dir: Path
) -> list[ContentBlock]:
    insertions = _build_insertions(variant, snippets, output_dir)
    return _render_blocks(variant, insertions)


def _block_to_myst(block: ContentBlock) -> str:
    if block.kind == "code":
        return f"```{block.lexer}\n{block.source}\n```\n"
    if block.kind == "output":
        # sphinx-design's dropdown gives a properly themed collapsible card
        # (border, header, chevron) for free, instead of an unstyled bare
        # <details> element.
        return (
            f"```{{dropdown}} Show output\n<pre>{html.escape(block.text)}</pre>\n```\n"
        )
    if block.kind == "image":
        # A real MyST/docutils figure node (unlike raw <img> HTML) is one
        # Sphinx tracks as an image dependency, so it gets copied into the
        # output tree automatically -- no manual copy step needed.
        return f"```{{figure}} {block.src}\n:alt: {block.alt}\n```\n"
    if block.kind == "video":
        # sphinxcontrib-video's directive (unlike a raw <video> tag) also
        # registers as a Sphinx image dependency, so it's copied into the
        # output tree automatically, same as the figure directive above.
        # Its default alignment is "left" (figure's is "default", which the
        # theme centers), so it needs an explicit override to match.
        return (
            f"```{{video}} {block.src}\n:alt: {block.alt}\n:align: center\n"
            ":autoplay:\n:loop:\n:muted:\n:playsinline:\n```\n"
        )
    raise ValueError(f"unknown content block kind: {block.kind!r}")


def lesson_asset_paths(lesson: Lesson, asset_root: Path) -> list[Path]:
    """Return this lesson's referenced source assets, including missing ones.

    Keeping a missing referenced path in the depfile lets Ninja retry that
    page after the asset is later added, without depending on the whole
    figures directory.
    """
    variants = [lesson.cpp]
    if lesson.python is not None:
        variants.append(lesson.python)
    return sorted(
        {
            asset_root / figure_ref
            for variant in variants
            for figure_refs in variant.figure_lines.values()
            for figure_ref in figure_refs
        }
    )


def lesson_input_paths(lesson: Lesson, asset_root: Path) -> list[Path]:
    """Return runtime image inputs referenced by this lesson's source.

    These aren't rendered site assets: they are read while lesson binaries or
    Python translations run to capture their output. Keep missing paths too,
    so Ninja retries a page when a newly referenced image is added.
    """
    variants = [lesson.cpp]
    if lesson.python is not None:
        variants.append(lesson.python)
    return sorted(
        {
            asset_root / "images" / name
            for variant in variants
            for name in _TUTORIAL_INPUT_IMAGE_RE.findall(
                variant.source_path.read_text()
            )
        }
    )


def _copy_assets(sources: list[Path], destination_dir: Path) -> None:
    """Atomically copy assets into one directory.

    Multiple lesson commands may copy the same figure concurrently. Each copy
    uses a private temporary file in the destination directory, then replaces
    the destination atomically, so concurrent writers cannot leave a partial
    asset behind.
    """
    destination_dir.mkdir(parents=True, exist_ok=True)
    for source in sources:
        if not source.is_file():
            continue
        destination = destination_dir / source.name
        with tempfile.NamedTemporaryFile(
            dir=destination_dir, prefix=f".{source.name}.", delete=False
        ) as tmp:
            tmp_path = Path(tmp.name)
        try:
            shutil.copy2(source, tmp_path)
            os.replace(tmp_path, destination)
        finally:
            tmp_path.unlink(missing_ok=True)


def copy_lesson_assets(
    lesson: Lesson, asset_root: Path, output_dir: Path, run_dir: Path
) -> None:
    """Stage a lesson's figures for Sphinx and images for capture execution."""
    _copy_assets(lesson_asset_paths(lesson, asset_root), output_dir / "figures")
    _copy_assets(lesson_input_paths(lesson, asset_root), run_dir / "images")


def _group_by_number(
    sitemap: list[SitemapEntry],
) -> list[tuple[int, list[SitemapEntry]]]:
    return [
        (number, list(group))
        for number, group in itertools.groupby(sitemap, key=lambda entry: entry.number)
    ]


def _title_parts(title: str) -> tuple[str, str | None]:
    base, separator, subtitle = title.partition(":")
    subtitle = subtitle.lstrip()
    if not separator or not subtitle:
        return title, None
    return base, subtitle[:1].upper() + subtitle[1:]


def _write_toctree(path: Path, title: str, entries: list[str]) -> None:
    lines = [
        f"# {title}",
        "",
        "```{toctree}",
        ":titlesonly:",
        ":maxdepth: 1",
        "",
        *entries,
        "```\n",
    ]
    path.write_text("\n".join(lines))


def render_index(sitemap: list[SitemapEntry], output_dir: Path) -> None:
    """Writes index.md plus one group wrapper page per multi-part lesson
    (10, 15, 16, 21) -- the same singleton-vs-group toctree idiom
    doc/generate_api_reference.py uses for the API reference, so a lesson
    number with a single source file links directly and one with several
    parts gets its own wrapper page over them."""
    output_dir.mkdir(parents=True, exist_ok=True)
    top_level_slugs = []
    for number, entries in _group_by_number(sitemap):
        if len(entries) == 1:
            top_level_slugs.append(entries[0].slug)
            continue

        base_title, _ = _title_parts(entries[0].title)
        group_slug = f"lesson_{number}"
        _write_toctree(
            output_dir / f"{group_slug}.md",
            f"{number}. {base_title}",
            [entry.slug for entry in entries],
        )
        top_level_slugs.append(group_slug)

    _write_toctree(output_dir / "index.md", "Tutorial", top_level_slugs)


_ANTI_FOUC_SCRIPT = """<script>
(function () {
  try {
    if (sessionStorage.getItem("sphinx-design-tab-id-tab") === "python") {
      document.documentElement.classList.add("pref-python");
    }
  } catch (e) {}
  // design-tabs.js's own <script> tag (in <head>) runs, and so registers
  // its DOMContentLoaded listener, before this inline script does (it's
  // encountered later, while parsing the body) -- so by the time this
  // listener fires, design-tabs.js has already set the correct radio's
  // `checked` state to match. Once that's true, the "pref-python" class
  // must come off: it forces Python's content to display via `!important`
  // (see custom.css) regardless of which radio is actually checked, which
  // is exactly what's needed to avoid a flash before that happens, but
  // would otherwise permanently override the plain `:checked`-based
  // styling for the rest of the page -- silently breaking a later click
  // back to C++.
  document.addEventListener("DOMContentLoaded", function () {
    document.documentElement.classList.remove("pref-python");
  });
})();
</script>"""


def _tab_set_myst(
    cpp_blocks: list[ContentBlock], python_blocks: list[ContentBlock]
) -> str:
    cpp_body = "\n".join(_block_to_myst(block) for block in cpp_blocks)
    python_body = "\n".join(_block_to_myst(block) for block in python_blocks)
    return (
        # sphinx-design's own design-tabs.js only restores a reader's saved
        # C++/Python choice on DOMContentLoaded -- i.e. after the whole page,
        # including this tab-set, has already been parsed and (on a long
        # lesson page) likely already painted with the C++ tab that's
        # statically `checked` in the markup below. This inline script runs
        # synchronously as the parser reaches it, before any of the tab-set
        # markup that follows has been parsed (let alone painted), so it can
        # add a class the CSS below uses to show the right tab from the very
        # first paint; design-tabs.js's own DOMContentLoaded handler then
        # reconciles the actual `checked` radio to match shortly after,
        # taking back over for any later clicks.
        '<div class="lesson-tabs">\n'
        f"{_ANTI_FOUC_SCRIPT}\n"
        "\n"
        "::::{tab-set}\n"
        ":::{tab-item} C++\n"
        ":sync: cpp\n"
        "\n"
        f"{cpp_body}\n"
        ":::\n"
        "\n"
        ":::{tab-item} Python\n"
        ":sync: python\n"
        "\n"
        f"{python_body}\n"
        ":::\n"
        "::::\n"
        "\n"
        "</div>\n"
    )


def render_lesson_page(
    lesson: Lesson,
    snippets: dict[int, str],
    python_snippets: dict[int, str],
    output_dir: Path,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    if lesson.python is None:
        body = "\n".join(
            _block_to_myst(block)
            for block in _variant_blocks(lesson.cpp, snippets, output_dir)
        )
    else:
        body = _tab_set_myst(
            _variant_blocks(lesson.cpp, snippets, output_dir),
            _variant_blocks(lesson.python, python_snippets, output_dir),
        )

    # Multi-part lessons (10, 15, 16, 21) have a "base: subtitle" title (e.g.
    # "Generators: writing a generator"); the base half is already the parent
    # group page's heading (see render_index), so the child page just needs
    # the subtitle on its own. Single lessons have no subtitle -- use the
    # lesson's own number and title, same "N. Title" style as group headings.
    _, subtitle = _title_parts(lesson.title)
    heading = subtitle or f"{lesson.number}. {lesson.title}"

    (output_dir / f"{lesson.slug}.md").write_text(f"# {heading}\n\n{body}")
