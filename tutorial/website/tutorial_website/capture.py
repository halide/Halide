"""Captures the stdout/stderr a lesson binary produces at specific source
lines, by driving GDB or LLDB in batch mode.

The technique (validated against both debuggers):

1. Set a one-shot breakpoint at every interesting line up front (both
   debuggers resolve breakpoints against not-yet-running local executables
   fine, so there's no need to separately "stop at main" first).
2. Launch the process with its OWN stdout/stderr redirected straight to two
   temp files -- not the debugger's stdout, which is discarded. This keeps
   the debugger's own command echo/breakpoint-hit messages completely
   separate from the program's real output.
3. At each stop: flush any output the target has already buffered, write an
   unbuffered BEGIN marker (raw write(2) to fds 1 and 2, not fprintf(stdout,
   ...)/fprintf(stderr, ...) -- LLDB's expression evaluator doesn't resolve
   the `stdout`/`stderr` macros without a #include, whereas write() needs no
   symbol beyond the fd number), step over exactly that one line, flush
   again so its output lands before the END marker, write the END marker,
   then continue to the next breakpoint.
4. Slice the two capture files between each line's BEGIN/END marker text.

This mirrors the interleaving trick the original gdb-only shell script used
(inferior-side fprintf calls bracketing each captured line), translated to
work identically under LLDB.
"""

from __future__ import annotations

import platform
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

_BEGIN = "BEGIN_SNIPPET_{}_"
_END = "END_SNIPPET_{}_"

_TIMEOUT_SECS = 300


@dataclass(frozen=True)
class DebuggerBackend:
    name: str  # "gdb" or "lldb"
    executable: Path


def find_backends(gdb: Path | None, lldb: Path | None) -> dict[str, Path]:
    found = {}
    if gdb:
        found["gdb"] = gdb
    if lldb:
        found["lldb"] = lldb
    return found


def pick_backend(available: dict[str, Path], preference: str) -> DebuggerBackend:
    preference = (preference or "auto").lower()
    if preference in ("gdb", "lldb"):
        if preference not in available:
            raise SystemExit(
                f"--prefer {preference} requested, but {preference} was not found"
            )
        return DebuggerBackend(preference, available[preference])

    # auto: prefer the platform-native debugger, since Homebrew gdb on macOS
    # generally isn't codesigned with debugging entitlements.
    order = ["lldb", "gdb"] if platform.system() == "Darwin" else ["gdb", "lldb"]
    for name in order:
        if name in available:
            return DebuggerBackend(name, available[name])
    raise SystemExit("no gdb or lldb executable available")


def _marker_writes(is_gdb: bool, marker: str) -> list[str]:
    text = marker + r"\n"
    n = len(marker) + 1
    call = "call" if is_gdb else "expression --"
    return [
        f'{call} (long)write(1, "{text}", {n})',
        f'{call} (long)write(2, "{text}", {n})',
    ]


def _snippet_commands(lines: list[int], is_gdb: bool) -> list[str]:
    flush = "call (int)fflush(0)" if is_gdb else "expression -- (int)fflush(0)"
    cmds = []
    for line in lines:
        cmds.append(flush)
        cmds += _marker_writes(is_gdb, _BEGIN.format(line))
        cmds.append("next")
        cmds.append(flush)
        cmds += _marker_writes(is_gdb, _END.format(line))
        cmds.append("continue")
    return cmds


def _build_args(
    backend: DebuggerBackend,
    binary: Path,
    source_name: str,
    lines: list[int],
    stdout_path: Path,
    stderr_path: Path,
) -> list[str]:
    is_gdb = backend.name == "gdb"
    if is_gdb:
        args = [
            str(backend.executable),
            "-nx",
            "-batch",
            "-ex",
            f"set args 2> {stderr_path} > {stdout_path}",
            "-ex",
            "set height 0",
            "-ex",
            "set width 0",
        ]
        for line in lines:
            args += ["-ex", f"tbreak {source_name}:{line}"]
        args += ["-ex", "run"]
        for cmd in _snippet_commands(lines, is_gdb=True):
            args += ["-ex", cmd]
        args += ["-ex", "quit", str(binary)]
        return args

    # Every lesson binary dynamically links the same handful of huge shared
    # libraries (notably libLLVM), so re-parsing their symbol tables from
    # scratch in every one of the ~20 lldb invocations this script makes
    # dominates the whole website build. LLDB's index cache persists parsed
    # symbol tables across invocations, keyed by module UUID, avoiding that.
    # The max-byte-size setting defaults to 0 (cache disabled for writes)
    # even with enable-lldb-index-cache true, so it must be raised explicitly.
    # Both settings must be enabled before the target (and its dependent
    # images) are created -- which is why the binary is loaded via an
    # explicit "target create" command here instead of the usual trailing
    # positional arg: that positional is processed before any "-o" command
    # runs, so enabling the cache via "-o" wouldn't take effect in time to
    # cover this launch.
    args = [
        str(backend.executable),
        "--batch",
        "-o",
        "settings set symbols.enable-lldb-index-cache true",
        "-o",
        "settings set symbols.lldb-index-cache-max-byte-size 1073741824",
        "-o",
        f"target create {binary}",
    ]
    for line in lines:
        args += [
            "-o",
            f"breakpoint set --file {source_name} --line {line} --one-shot true",
        ]
    args += ["-o", f"process launch -o {stdout_path} -e {stderr_path} --"]
    for cmd in _snippet_commands(lines, is_gdb=False):
        args += ["-o", cmd]
    args += ["-o", "quit"]
    return args


def _slice_markers(text: str, lines: list[int]) -> dict[int, str]:
    all_lines = text.splitlines()
    result = {}
    for line in lines:
        begin, end = _BEGIN.format(line), _END.format(line)
        start_idx = next(
            (i for i, ln in enumerate(all_lines) if ln.strip() == begin), None
        )
        if start_idx is None:
            continue
        collected = []
        for ln in all_lines[start_idx + 1 :]:
            if ln.strip() == end:
                break
            collected.append(ln)
        chunk = "\n".join(collected).strip("\n")
        if chunk:
            result[line] = chunk
    return result


def capture_snippets(
    backend: DebuggerBackend,
    binary: Path,
    cwd: Path,
    source_name: str,
    lines: list[int],
) -> dict[int, str]:
    if not lines:
        return {}

    with tempfile.TemporaryDirectory() as tmp:
        stdout_path = Path(tmp) / "stdout.txt"
        stderr_path = Path(tmp) / "stderr.txt"
        args = _build_args(
            backend, binary, source_name, lines, stdout_path, stderr_path
        )
        subprocess.run(
            args,
            cwd=cwd,
            capture_output=True,
            text=True,
            timeout=_TIMEOUT_SECS,
            check=False,
        )
        stdout_text = stdout_path.read_text() if stdout_path.exists() else ""
        stderr_text = stderr_path.read_text() if stderr_path.exists() else ""

    stdout_chunks = _slice_markers(stdout_text, lines)
    stderr_chunks = _slice_markers(stderr_text, lines)

    result = {}
    for line in lines:
        parts = [p for p in (stdout_chunks.get(line), stderr_chunks.get(line)) if p]
        if parts:
            result[line] = "\n".join(parts)
    return result


def capture_env_output(binary: Path, cwd: Path, env_overrides: dict[str, str]) -> str:
    """Runs the whole binary with extra environment variables set and returns
    stderr -- used for lesson 3's HL_DEBUG_CODEGEN=1 walkthrough, which has
    no single interesting line to break at."""
    import os

    env = {**os.environ, **env_overrides}
    proc = subprocess.run(
        [str(binary)],
        cwd=cwd,
        env=env,
        capture_output=True,
        text=True,
        timeout=_TIMEOUT_SECS,
    )
    return proc.stderr


def debugger_available(name: str) -> bool:
    return shutil.which(name) is not None
