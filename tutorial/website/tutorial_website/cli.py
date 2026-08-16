from __future__ import annotations

import argparse
import sys
from pathlib import Path

from . import capture
from .lesson import discover_lessons
from .manifest import load_manifest
from .render import render_assets, render_index, render_lesson_page
from .sitemap import build_sitemap, load_sitemap, write_sitemap


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Generate the Halide tutorials website"
    )
    subparsers = parser.add_subparsers(dest="mode", required=True)

    sitemap_parser = subparsers.add_parser(
        "sitemap", help="write sitemap.json (cheap per-lesson metadata for navigation)"
    )
    sitemap_parser.add_argument("--tutorial-dir", type=Path, required=True)
    sitemap_parser.add_argument("--output", type=Path, required=True)

    assets_parser = subparsers.add_parser(
        "assets", help="materialize shared assets (css/figures) and index.html"
    )
    assets_parser.add_argument("--tutorial-dir", type=Path, required=True)
    assets_parser.add_argument("--sitemap", type=Path, required=True)
    assets_parser.add_argument("--output-dir", type=Path, required=True)

    lesson_parser = subparsers.add_parser("lesson", help="render one lesson's page")
    lesson_parser.add_argument("--slug", required=True)
    lesson_parser.add_argument(
        "--manifest", type=Path, required=True, help="source->binary JSON manifest"
    )
    lesson_parser.add_argument(
        "--tutorial-dir", type=Path, required=True, help="tutorial/ directory"
    )
    lesson_parser.add_argument("--sitemap", type=Path, required=True)
    lesson_parser.add_argument(
        "--run-cwd",
        type=Path,
        required=True,
        help="directory to run lesson binaries from",
    )
    lesson_parser.add_argument(
        "--output-dir", type=Path, required=True, help="output site directory"
    )
    lesson_parser.add_argument(
        "--prefer", default="auto", choices=["auto", "gdb", "lldb"]
    )
    lesson_parser.add_argument("--gdb", type=Path, default=None)
    lesson_parser.add_argument("--lldb", type=Path, default=None)

    args = parser.parse_args(argv)
    if args.mode == "sitemap":
        return _main_sitemap(args)
    if args.mode == "assets":
        return _main_assets(args)
    return _main_lesson(args)


def _main_sitemap(args: argparse.Namespace) -> int:
    entries = build_sitemap(args.tutorial_dir)
    if not entries:
        print(
            f"error: no lesson_*.cpp/lesson_*.sh files found under {args.tutorial_dir}",
            file=sys.stderr,
        )
        return 1
    write_sitemap(entries, args.output)
    return 0


def _main_assets(args: argparse.Namespace) -> int:
    sitemap = load_sitemap(args.sitemap)
    render_assets(args.tutorial_dir / "figures", args.output_dir)
    render_index(sitemap, args.output_dir)
    return 0


def _main_lesson(args: argparse.Namespace) -> int:
    manifest = load_manifest(args.manifest)
    lessons = discover_lessons(args.tutorial_dir, manifest)
    lesson = next((c for c in lessons if c.slug == args.slug), None)
    if lesson is None:
        print(f"error: no lesson found with slug {args.slug!r}", file=sys.stderr)
        return 1

    sitemap = load_sitemap(args.sitemap)

    available = capture.find_backends(args.gdb, args.lldb)
    backend = capture.pick_backend(available, args.prefer) if available else None
    if backend is None and lesson.interesting_lines:
        print(
            f"warning: no gdb/lldb backend available; rendering {lesson.slug} "
            "without captured output",
            file=sys.stderr,
        )

    snippets: dict[int, str] = {}
    if lesson.binary_path is not None and lesson.binary_path.exists():
        if lesson.interesting_lines and backend is not None:
            snippets = capture.capture_snippets(
                backend,
                lesson.binary_path,
                args.run_cwd,
                lesson.source_path.name,
                lesson.interesting_lines,
            )

        if lesson.env_capture_line is not None:
            output = capture.capture_env_output(
                lesson.binary_path, args.run_cwd, lesson.env_capture_vars
            )
            if output.strip():
                snippets[lesson.env_capture_line] = output.strip()

    render_lesson_page(lesson, sitemap, snippets, args.output_dir)
    print(f"Rendered {lesson.slug}.html")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
