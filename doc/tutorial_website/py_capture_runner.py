#!/usr/bin/env python3
"""Runs a Halide Python tutorial script under sys.settrace, bracketing each
requested source line with the same BEGIN_SNIPPET_{}_/END_SNIPPET_{}_ markers
capture.py's gdb/lldb path writes via inferior calls to write(2, ...), so
capture.py's _slice_markers() can extract the output either path produces
without caring which one ran.

Invoked as its own subprocess (by capture.capture_python_snippets) using
whichever Python interpreter has the built `halide` package importable --
not necessarily the interpreter running the site generator itself, which
only needs jinja2/pygments/markupsafe -- so this file is stdlib-only and
self-contained: it can't import anything else from tutorial_website.

The technique: gdb/lldb bracket a line by setting a one-shot breakpoint,
running to it, then single-stepping "next" over exactly that one source
line. sys.settrace's 'line' event fires *before* each line executes, which
gives the same two edges without a debugger: opening a bracket when
execution reaches the next requested line, and closing it on whichever
happens first for that same frame -- the next 'line' event (i.e. that
statement finished) or a 'return' event (i.e. it was the function's last
line). Watching only 'line'/'return' events for the specific frame a bracket
was opened in (rather than every frame sys.settrace is invoked for) means
output from a call the bracketed line makes into another function in the
same file -- e.g. a Generator's generate() method -- still counts as part of
that line's bracket, exactly like stepping over a call in a debugger does.
"""

from __future__ import annotations

import os
import runpy
import sys

_BEGIN = "BEGIN_SNIPPET_{}_"
_END = "END_SNIPPET_{}_"


def _write_marker(text: str) -> None:
    data = (text + "\n").encode()
    os.write(1, data)
    os.write(2, data)


def _capture(script_path: str, lines: list[int]) -> None:
    target_path = os.path.realpath(script_path)
    pending = sorted(lines)
    open_line: int | None = None
    open_frame = None

    def close_bracket() -> None:
        nonlocal open_line, open_frame
        if open_line is not None:
            sys.stdout.flush()
            sys.stderr.flush()
            _write_marker(_END.format(open_line))
            open_line = None
            open_frame = None

    def trace_lines(frame, event, arg):
        nonlocal open_line, open_frame
        if event == "line":
            if open_frame is frame and frame.f_lineno != open_line:
                close_bracket()
            if pending and frame.f_lineno == pending[0]:
                line = pending.pop(0)
                sys.stdout.flush()
                sys.stderr.flush()
                _write_marker(_BEGIN.format(line))
                open_line = line
                open_frame = frame
            if not pending and open_line is None:
                # Every requested line has been seen and closed out (or,
                # for lines still pending, never reached at all -- e.g. a
                # GPU-only code path with no GPU present -- same as a gdb
                # one-shot breakpoint that's never hit). Stop tracing early
                # rather than paying settrace's per-line overhead for the
                # rest of the script.
                sys.settrace(None)
                return None
        elif event == "return":
            if open_frame is frame:
                close_bracket()
        return trace_lines

    def trace_calls(frame, event, arg):
        if (
            event == "call"
            and os.path.realpath(frame.f_code.co_filename) == target_path
        ):
            return trace_lines
        return None

    sys.settrace(trace_calls)
    try:
        runpy.run_path(script_path, run_name="__main__")
    finally:
        sys.settrace(None)
        close_bracket()


if __name__ == "__main__":
    _capture(sys.argv[1], [int(arg) for arg in sys.argv[2:]])
