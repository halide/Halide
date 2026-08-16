"""Discovers lesson_*.cpp/lesson_*.sh files and the conventions their prose
already uses:

- Every lesson has a `// Halide tutorial lesson N: Title` (or `#`-commented,
  for the one shell-script lesson) line near the top -- not necessarily the
  very first line, since shell scripts lead with a shebang.
- Lines that call `.realize(`, `tick(`, `print_loop_nest()`, or print
  "Printing a complex Expr" are worth capturing output for.
- Comments referencing `figures/lesson_XX_....{gif,mp4,jpg,png}` mark a
  pre-rendered static asset to inline.

None of this requires any changes to the lesson sources themselves.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

TITLE_RE = re.compile(r"^(?://|#)\s*Halide tutorial lesson\s+\d+:\s*(.+?)\s*$")
NUMBER_RE = re.compile(r"^lesson_(\d+)_")
FIGURE_RE = re.compile(r"figures/lesson_\w+\.(?:gif|mp4|jpg|jpeg|png)")
BLANK_LINE_RE = re.compile(r"^\s*$")
INTERESTING_LINE_RE = re.compile(
    r"tick\(|\.realize\(|print_loop_nest\(\)|Printing a complex Expr"
)
COMMENT_ONLY_RE = re.compile(r"^\s*(?://|#)")

# Maps a source file's suffix to the Pygments lexer name highlight.py should
# use for it.
LEXERS = {".cpp": "cpp", ".sh": "bash"}

# Lessons that are best understood by re-running the whole binary with a
# debug env var set, rather than by capturing a single statement's output.
# Keyed by lesson number (not by grepping for the env var name generically,
# since more than one lesson's prose happens to mention HL_DEBUG_CODEGEN).
ENV_CAPTURE_LESSONS: dict[int, tuple[str, dict[str, str]]] = {
    3: ("HL_DEBUG_CODEGEN", {"HL_DEBUG_CODEGEN": "1"}),
}


@dataclass
class Lesson:
    number: int
    slug: str
    title: str
    lexer: str
    source_path: Path
    lines: list[str]
    display_lines: list[str]
    interesting_lines: list[int]
    figure_lines: dict[int, list[str]]
    env_capture_line: int | None = None
    env_capture_vars: dict[str, str] = field(default_factory=dict)
    binary_path: Path | None = None


def _next_blank_line(lines: list[str], from_line: int) -> int:
    """Returns the 1-indexed line number of the first blank line at or after
    `from_line + 1`, or the file's last line if the comment block runs to
    EOF without one."""
    for i in range(from_line, len(lines)):
        if BLANK_LINE_RE.match(lines[i]):
            return i + 1
    return len(lines)


def discover_lessons(tutorial_dir: Path, manifest: dict[str, Path]) -> list[Lesson]:
    sources = sorted(
        [*tutorial_dir.glob("lesson_*.cpp"), *tutorial_dir.glob("lesson_*.sh")]
    )
    lessons = []
    for source_path in sources:
        number_match = NUMBER_RE.match(source_path.name)
        if not number_match:
            continue
        number = int(number_match.group(1))

        lines = source_path.read_text().splitlines()
        title = source_path.stem
        # The title comment isn't necessarily the first line -- shell-script
        # lessons lead with a shebang -- so scan for it near the top instead
        # of assuming lines[0].
        for line in lines[:5]:
            title_match = TITLE_RE.match(line)
            if title_match:
                title = title_match.group(1)
                break

        env_marker = ENV_CAPTURE_LESSONS.get(number)
        env_line = None
        env_vars: dict[str, str] = {}
        interesting_lines = []
        display_lines = list(lines)
        figure_lines: dict[int, list[str]] = {}

        for i, line in enumerate(lines, start=1):
            figure_match = FIGURE_RE.search(line)
            if figure_match:
                # The prose reads naturally with the raw path replaced by
                # "below", and the figure itself is placed after the whole
                # comment block it belongs to (i.e. at the next blank line),
                # not immediately after the line that happens to mention it.
                display_lines[i - 1] = FIGURE_RE.sub("below", line)
                insertion_line = _next_blank_line(lines, i)
                figure_lines.setdefault(insertion_line, []).append(
                    figure_match.group(0)
                )

            if env_marker and env_line is None and env_marker[0] in line:
                # Same reasoning as figures: place it after the whole comment
                # block, not mid-paragraph where the env var happens to be
                # mentioned.
                env_line = _next_blank_line(lines, i)
                env_vars = env_marker[1]

            if COMMENT_ONLY_RE.match(line):
                continue
            if INTERESTING_LINE_RE.search(line):
                interesting_lines.append(i)

        lessons.append(
            Lesson(
                number=number,
                slug=source_path.stem,
                title=title,
                lexer=LEXERS.get(source_path.suffix, "text"),
                source_path=source_path,
                lines=lines,
                display_lines=display_lines,
                interesting_lines=interesting_lines,
                figure_lines=figure_lines,
                env_capture_line=env_line,
                env_capture_vars=env_vars,
                binary_path=manifest.get(source_path.name),
            )
        )
    return lessons
