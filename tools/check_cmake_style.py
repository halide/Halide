#!/usr/bin/env python3
"""Enforce Halide's CMake coding standards from doc/CodeStyleCMake.md."""

import re
import sys
from pathlib import Path

# fmt: off

PROHIBITED_COMMANDS: dict[str, str] = {
    "add_compile_definitions":       "use target_compile_definitions",
    "add_compile_options":           "use target_compile_options",
    "add_definitions":               "use target_compile_definitions",
    "add_link_options":              "use target_link_options",
    "aux_source_directory":          "list source files explicitly",
    "build_command":                 "use CMAKE_CTEST_COMMAND",
    "cmake_host_system_information": "inspect toolchain variables instead",
    "create_test_sourcelist":        "use Halide's own testing solution",
    "define_property":               "use a cache variable",
    "enable_language":               "Halide is C/C++ only",
    "fltk_wrap_ui":                  "Halide does not use FLTK",
    "include_directories":           "use target_include_directories",
    "include_external_msproject":    "write a CMake package config file",
    "include_guard":                 "use functions, not recursive inclusion",
    "include_regular_expression":    "changes default dependency checking",
    "link_directories":              "use target_link_libraries",
    "link_libraries":                "use target_link_libraries",
    "load_cache":                    "write a vcpkg port instead",
    "macro":                         "use function() instead",
    "remove_definitions":            "use target_compile_definitions with genexes",
    "set_directory_properties":      "use cache variables or target properties",
    "site_name":                     "privacy: do not leak host name",
    "variable_watch":                "debugging helper, not for production",
}

# fmt: on

# Patterns that need special handling beyond simple command detection.
SPECIAL_PATTERNS: list[tuple[re.Pattern[str], str]] = [
    (
        re.compile(r"\bcmake_policy\s*\(.*\bOLD\b", re.IGNORECASE),
        "cmake_policy(... OLD) is deprecated; fix code for new policy",
    ),
    (
        re.compile(r"\bfile\s*\(\s*GLOB", re.IGNORECASE),
        "file(GLOB ...) interacts poorly with incremental builds; list files explicitly",
    ),
    (
        re.compile(r"\bset_property\s*\(\s*DIRECTORY\b", re.IGNORECASE),
        "set_property(DIRECTORY) is prohibited; use cache variables or target properties",
    ),
    (
        re.compile(
            r"\btarget_link_libraries\s*\(\s*\S+\s+(?!PRIVATE|PUBLIC|INTERFACE)\S",
            re.IGNORECASE,
        ),
        "target_link_libraries without visibility specifier; add PRIVATE, PUBLIC, or INTERFACE",
    ),
]

# Build a single regex for all prohibited commands: matches the command name
# followed by '(' with optional whitespace, case-insensitive.
_CMD_PATTERN = re.compile(
    r"\b(" + "|".join(re.escape(c) for c in PROHIBITED_COMMANDS) + r")\s*\(",
    re.IGNORECASE,
)

_COMMENT_RE = re.compile(r"^\s*#")


def check_file(path: Path):
    text = path.read_text()

    for lineno, line in enumerate(text.splitlines(), start=1):
        if _COMMENT_RE.match(line):
            continue

        if "#" in line:
            code, comment = line.split("#", 1)
        else:
            code, comment = line, ""

        if "nolint" in comment:
            continue

        for m in _CMD_PATTERN.finditer(code):
            cmd = m.group(1).lower()
            reason = PROHIBITED_COMMANDS[cmd]
            yield f"{path}:{lineno}: {cmd}() is prohibited; {reason}"

        for pattern, message in SPECIAL_PATTERNS:
            if pattern.search(code):
                yield f"{path}:{lineno}: {message}"


def main():
    status = 0
    for arg in sys.argv[1:]:
        for error in check_file(Path(arg)):
            print(error, file=sys.stderr)
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
