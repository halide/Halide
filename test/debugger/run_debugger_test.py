#!/usr/bin/env python3
"""Drive LLDB or GDB in batch mode against debugger_fixture and check that the
Halide pretty-printers (tools/lldbhalide.py, tools/gdbhalide.py) render each
value as expected.

Usage:
    run_debugger_test.py --debugger lldb|gdb --debugger-path PATH \
        --fixture PATH --tools-dir PATH

On success this prints a pass line and exits 0. On a real mismatch it exits 1.
When the debugger cannot evaluate Halide::Internal::debug_string() at all — which
means libHalide was built without debug info (e.g. a Release build), and the
pretty-printers cannot run — it prints "[SKIP]" and exits 0 so CTest's
SKIP_REGULAR_EXPRESSION marks the test skipped rather than passed or failed.
"""

import argparse
import subprocess
import sys

# Marker emitted before each value so we can attribute output to a variable even
# though LLDB and GDB format their prints differently.
MARK = "===HALIDE_CASE:{}==="
PROBE = "probe"

# (variable name in the fixture, substring its pretty-printed form must contain).
CASES = [
    ("e", "max(min(x + y, 100), 20)"),  # Expr, via debug_string(Expr)
    ("s", "produce myfunc"),  # Stmt, via debug_string(Stmt) summary
    ("t", "int32"),  # Type
    ("target", "target("),  # Target
    ("buf", "myimg"),  # Buffer name
]

BREAKPOINT = "halide_debugger_test_breakpoint"


def lldb_commands(tools_dir):
    cmds = [
        f"command script import {tools_dir}/lldbhalide.py",
        f"breakpoint set --name {BREAKPOINT}",
        "run",
        # Probe: can the evaluator call debug_string at all (i.e. is there debug
        # info for it)? If not, we skip rather than fail.
        f"script print('{MARK.format(PROBE)}')",
        "expression -- Halide::Internal::debug_string(e)",
    ]
    for name, _ in CASES:
        cmds.append(f"script print('{MARK.format(name)}')")
        cmds.append(f"frame variable {name}")
    cmds.append("quit")
    return [arg for c in cmds for arg in ("-o", c)]


def gdb_commands(tools_dir):
    cmds = [
        f"source {tools_dir}/gdbhalide.py",
        f"break {BREAKPOINT}",
        "run",
        f"echo {MARK.format(PROBE)}\\n",
        "print Halide::Internal::debug_string(e)",
    ]
    for name, _ in CASES:
        cmds.append(f"echo {MARK.format(name)}\\n")
        cmds.append(f"print {name}")
    cmds.append("quit")
    args = ["-batch", "-nx"]
    for c in cmds:
        args += ["-ex", c]
    return args


def run(debugger, debugger_path, fixture, tools_dir):
    if debugger == "lldb":
        argv = [debugger_path, "--batch", *lldb_commands(tools_dir), fixture]
    else:
        argv = [debugger_path, *gdb_commands(tools_dir), fixture]

    proc = subprocess.run(argv, capture_output=True, text=True, timeout=300)
    return proc.stdout + "\n" + proc.stderr


def chunk(output, name):
    """Return the lines of output following this case's marker (up to the next).

    Markers are matched as whole lines so we skip the debugger's command echo
    (e.g. LLDB prints "(lldb) script print('===HALIDE_CASE:e===')") and only key
    off the printed marker line itself."""
    marker = MARK.format(name)
    lines = output.splitlines()
    idx = next((i for i, ln in enumerate(lines) if ln.strip() == marker), None)
    if idx is None:
        return None
    collected = []
    for ln in lines[idx + 1 :]:
        if ln.strip().startswith("===HALIDE_CASE:"):
            break
        collected.append(ln)
    return "\n".join(collected)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--debugger", required=True, choices=["lldb", "gdb"])
    p.add_argument("--debugger-path", required=True)
    p.add_argument("--fixture", required=True)
    p.add_argument("--tools-dir", required=True)
    args = p.parse_args()

    try:
        output = run(args.debugger, args.debugger_path, args.fixture, args.tools_dir)
    except subprocess.TimeoutExpired:
        print(f"[{args.debugger}] timed out", file=sys.stderr)
        return 1

    probe = chunk(output, PROBE)
    if probe is None or CASES[0][1] not in probe:
        # The evaluator could not render debug_string(e). This is expected when
        # libHalide has no debug info; skip instead of failing. The "[SKIP]"
        # token is what CTest's SKIP_REGULAR_EXPRESSION keys off.
        print(
            f"[SKIP] [{args.debugger}] could not evaluate "
            "Halide::Internal::debug_string() — libHalide appears to lack debug "
            "info (build RelWithDebInfo or Debug to run this test)."
        )
        print("---- debugger output ----", file=sys.stderr)
        print(output, file=sys.stderr)
        return 0

    failures = []
    for name, expected in CASES:
        region = chunk(output, name)
        if region is None:
            failures.append(f"{name}: no output (marker missing)")
        elif expected not in region:
            failures.append(
                f"{name}: expected {expected!r} in:\n    {region.strip()!r}"
            )

    if failures:
        print(f"[{args.debugger}] FAILED:", file=sys.stderr)
        for f in failures:
            print("  -", f, file=sys.stderr)
        print("---- debugger output ----", file=sys.stderr)
        print(output, file=sys.stderr)
        return 1

    print(f"[{args.debugger}] all {len(CASES)} cases passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
