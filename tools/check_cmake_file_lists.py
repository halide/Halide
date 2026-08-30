#!/usr/bin/env python3
"""Check that every CMakeLists.txt names the .c/.cpp files next to it."""

import re
import subprocess
import sys
from pathlib import Path

SOURCE_RE = re.compile(r"\b[A-Za-z0-9_]+\.(?:cpp|c)\b")


def ignored(directory: Path, names: list[str]) -> set[str]:
    """The subset of names git is configured to ignore.

    A file git ignores is somebody's local scratch pad rather than part of
    the build, so it has no business being named in a CMakeLists.txt.
    """
    if not names:
        return set()
    try:
        result = subprocess.run(
            ["git", "check-ignore", "--stdin"],
            cwd=directory,
            input="\n".join(names),
            capture_output=True,
            text=True,
        )
    except OSError:
        # No git available, so treat everything as part of the build.
        return set()
    return set(result.stdout.split())


def check_dir(cmake_file: Path) -> list[str]:
    directory = cmake_file.parent

    source_files = sorted(
        p.name
        for p in directory.iterdir()
        if p.suffix in (".c", ".cpp") and p.is_file()
    )
    source_files = [f for f in source_files if f not in ignored(directory, source_files)]
    if not source_files:
        return []

    mentioned = set(SOURCE_RE.findall(cmake_file.read_text()))
    return [f for f in source_files if f not in mentioned]


def main() -> int:
    status = 0

    for path in sys.argv[1:]:
        cmake_file = Path(path)
        if missing := check_dir(cmake_file):
            print(f"{cmake_file}:", file=sys.stderr)
            for f in missing:
                print(f"  {f}", file=sys.stderr)
            status = 1

    return status


if __name__ == "__main__":
    sys.exit(main())
