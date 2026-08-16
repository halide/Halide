from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from . import capture
from .lesson import discover_lessons
from .manifest import load_manifest, load_python_env
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
        "assets", help="materialize shared assets (css/js/figures) and index.html"
    )
    assets_parser.add_argument("--tutorial-dir", type=Path, required=True)
    assets_parser.add_argument("--sitemap", type=Path, required=True)
    assets_parser.add_argument("--output-dir", type=Path, required=True)
    assets_parser.add_argument(
        "--python-manifest",
        type=Path,
        required=True,
        help="JSON manifest of the Python interpreter/PYTHONPATH that can "
        "`import halide`, checked once up front so a systemic capture "
        "failure across every Python lesson surfaces as one clear warning "
        "instead of silently degrading each lesson individually",
    )

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
    lesson_parser.add_argument(
        "--python-manifest",
        type=Path,
        required=True,
        help="JSON manifest of the Python interpreter/PYTHONPATH that can "
        "`import halide`, for capturing output from Python lessons",
    )

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

    # Checked once, here, rather than inferred after the fact from every
    # Python lesson capturing zero output: that inference needs every
    # lesson's result in one place, which no longer exists now that each
    # lesson renders in its own process.
    python_env = load_python_env(args.python_manifest)
    check = subprocess.run(
        [str(python_env.executable), "-c", "import halide"],
        env=_import_check_env(python_env),
        capture_output=True,
        text=True,
    )
    if check.returncode != 0:
        print(
            f"warning: {python_env.executable} cannot `import halide`; Python "
            f"lessons will render without captured output (PYTHONPATH: "
            f"{python_env.pythonpath})",
            file=sys.stderr,
        )
    return 0


def _import_check_env(python_env) -> dict[str, str]:
    import os

    env = dict(os.environ)
    if python_env.pythonpath:
        parts = [str(p) for p in python_env.pythonpath]
        existing = env.get("PYTHONPATH")
        if existing:
            parts.append(existing)
        env["PYTHONPATH"] = os.pathsep.join(parts)
    return env


def _main_lesson(args: argparse.Namespace) -> int:
    manifest = load_manifest(args.manifest)
    lessons = discover_lessons(args.tutorial_dir, manifest)
    lesson = next((c for c in lessons if c.slug == args.slug), None)
    if lesson is None:
        print(f"error: no lesson found with slug {args.slug!r}", file=sys.stderr)
        return 1

    sitemap = load_sitemap(args.sitemap)
    python_env = load_python_env(args.python_manifest)

    available = capture.find_backends(args.gdb, args.lldb)
    backend = capture.pick_backend(available, args.prefer) if available else None
    if backend is None and lesson.cpp.interesting_lines:
        print(
            f"warning: no gdb/lldb backend available; rendering {lesson.slug} "
            "without captured C++ output",
            file=sys.stderr,
        )

    snippets: dict[int, str] = {}
    if lesson.binary_path is not None and lesson.binary_path.exists():
        if lesson.cpp.interesting_lines and backend is not None:
            snippets = capture.capture_snippets(
                backend,
                lesson.binary_path,
                args.run_cwd,
                lesson.cpp.source_path.name,
                lesson.cpp.interesting_lines,
            )

        if lesson.cpp.env_capture_line is not None:
            output = capture.capture_env_output(
                [str(lesson.binary_path)], args.run_cwd, lesson.cpp.env_capture_vars
            )
            if output.strip():
                snippets[lesson.cpp.env_capture_line] = output.strip()

    python_snippets: dict[int, str] = {}
    if lesson.python is not None:
        if lesson.python.interesting_lines:
            python_snippets = capture.capture_python_snippets(
                python_env.executable,
                lesson.python.source_path,
                args.run_cwd,
                lesson.python.interesting_lines,
                pythonpath=python_env.pythonpath,
            )

        if lesson.python.env_capture_line is not None:
            output = capture.capture_python_env_output(
                python_env.executable,
                lesson.python.source_path,
                args.run_cwd,
                lesson.python.env_capture_vars,
                pythonpath=python_env.pythonpath,
            )
            if output.strip():
                python_snippets[lesson.python.env_capture_line] = output.strip()

    render_lesson_page(lesson, sitemap, snippets, python_snippets, args.output_dir)
    print(f"Rendered {lesson.slug}.html")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
