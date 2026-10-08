#!/usr/bin/env python3
"""CTest launcher that rejects use of undeclared Halide test capabilities.

Usage: audit_test_labels.py --build-dir BUILD [--config CONFIG] -- COMMAND [ARGS...]

CTest supplies the environment, working directory, timeout and result policy.
The launcher reads the test's actual labels from CTest, without changing its
target. Extra permissions are allowed: a test may skip a capability on a host
that does not support it.
"""

import argparse
import contextlib
import json
import os
import pathlib
import signal
import subprocess
import sys
import tempfile

FAILURE_MARKER = "Halide test capability violation:"
SUCCESS_MARKER = "Halide test launcher: successful exit"
PARALLEL_MARKER = "Scheduling parallel loop: "
JIT_MARKER = "Running JIT code"
GPU_TARGET_QUERY_MARKER = "Querying target GPU capability: "
MARKERS = {
    "target_from_environment": ("Reading target from environment: ",),
    "calls_llvm": ("Loading runtime bitcode: ",),
    "gpu": ("Using device interface: ", "Compiling GPU kernel: "),
}


def ctest_metadata(build_dir, config, ctest, refresh=False):
    cache_dir = pathlib.Path(build_dir) / "test-label-audit"
    if config:
        cache_dir /= config
    generation = (cache_dir / "generation.txt").read_text(encoding="utf-8").strip()
    cache_file = cache_dir / f"metadata-{generation}.json"
    if not refresh:
        try:
            with cache_file.open(encoding="utf-8") as f:
                return json.load(f)["tests"]
        except FileNotFoundError:
            pass

    cmd = [ctest, "--test-dir", build_dir, "--show-only=json-v1"]
    if config:
        cmd += ["-C", config]
    tests = json.loads(subprocess.check_output(cmd))["tests"]
    # Publish an immutable snapshot so concurrent readers and writers are safe
    # even on Windows. Reconfiguration changes the generation token.
    with tempfile.TemporaryDirectory(dir=cache_dir) as tmp:
        snapshot = pathlib.Path(tmp) / "metadata.json"
        with snapshot.open("w", encoding="utf-8") as f:
            json.dump({"tests": tests}, f)
        with contextlib.suppress(FileExistsError):
            os.link(snapshot, cache_file)
    return tests


def test_metadata(build_dir, config, ctest, command):
    tests = ctest_metadata(build_dir, config, ctest)
    # CTest's command includes this launcher and, when applicable, an emulator.
    matches = [t for t in tests if t.get("command", [])[-len(command) :] == command]
    if not matches:
        # CTest omits commands for executables that have not been built yet.
        tests = ctest_metadata(build_dir, config, ctest, refresh=True)
        matches = [t for t in tests if t.get("command", [])[-len(command) :] == command]
    if not matches:
        raise ValueError(f"No CTest test matches command {command!r}")
    # Several tests may share a command. It must be permitted by all of them.
    labels = [
        set(
            next(
                (p["value"] for p in t.get("properties", []) if p["name"] == "LABELS"),
                [],
            )
        )
        for t in matches
    ]
    expected_failures = {
        next(
            (p["value"] for p in t.get("properties", []) if p["name"] == "WILL_FAIL"),
            False,
        )
        for t in matches
    }
    if len(expected_failures) != 1:
        raise ValueError("Tests sharing a command must agree on WILL_FAIL")
    pass_expressions = {
        bool(
            [
                regex
                for p in t.get("properties", [])
                if p["name"] == "PASS_REGULAR_EXPRESSION"
                for regex in p["value"]
                if regex not in (FAILURE_MARKER, SUCCESS_MARKER)
            ]
        )
        for t in matches
    }
    if len(pass_expressions) != 1:
        raise ValueError("Tests sharing a command must agree on pass-expression policy")
    return (
        ", ".join(t["name"] for t in matches),
        set.intersection(*labels),
        expected_failures.pop(),
        pass_expressions.pop(),
    )


def uses_gpu(output):
    previous = ""
    for line in output.splitlines():
        if any(marker in line for marker in MARKERS["gpu"]) and not (
            previous.startswith(GPU_TARGET_QUERY_MARKER)
            and line
            == "Using device interface: " + previous[len(GPU_TARGET_QUERY_MARKER) :]
        ):
            return True
        previous = line
    return False


def missing_capabilities(labels, output):
    missing = [
        label
        for label, markers in MARKERS.items()
        if label != "gpu"
        and label not in labels
        and any(marker in output for marker in markers)
    ]
    if "gpu" not in labels and uses_gpu(output):
        missing.append("gpu")
    parallel = output.find(PARALLEL_MARKER)
    if (
        "multithreaded" not in labels
        and parallel >= 0
        and JIT_MARKER in output[parallel:]
    ):
        missing.append("multithreaded")
    return missing


def main():
    # CTest consumes UTF-8 output even when Python defaults to a Windows code page.
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")

    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--config", default=None)
    ap.add_argument("--ctest", default="ctest")
    ap.add_argument("command", nargs=argparse.REMAINDER)
    args = ap.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        ap.error("a test command is required after --")

    try:
        name, labels, will_fail, has_pass_expression = test_metadata(
            args.build_dir, args.config, args.ctest, command
        )
    except (OSError, subprocess.CalledProcessError, ValueError, KeyError) as e:
        print(f"{FAILURE_MARKER} cannot read test metadata: {e}", file=sys.stderr)
        return 1

    env = dict(os.environ)
    # Preserve requested debug output while enabling the audit's tagged events.
    tags = [
        tag
        for label, tag in (
            ("target_from_environment", "target-env"),
            ("calls_llvm", "llvm-entry"),
            ("multithreaded", "parallel-schedule"),
            ("multithreaded", "jit-execution"),
            ("gpu", "gpu-entry"),
        )
        if label not in labels
    ]
    if tags:
        env["HL_DEBUG_CODEGEN"] = ";".join(
            filter(None, (env.get("HL_DEBUG_CODEGEN"), "0;tag:" + ",".join(tags)))
        )
        env["HL_DEBUG_CODEGEN_LOG_FILE"] = "/dev/stderr"
    try:
        result = subprocess.run(
            command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )
    except OSError as e:
        print(f"{FAILURE_MARKER} cannot launch {name}: {e}", file=sys.stderr)
        return 1

    output = result.stdout.decode(errors="replace")
    missing = missing_capabilities(labels, output)
    if missing:
        # CTest gives skip expressions precedence over failure expressions.
        # Keep the diagnostics, but do not let Halide's skip token mask a violation.
        output = output.replace("[SKIP", "[AUDIT-INVALID-SKIP")
        if will_fail:
            # Only the audit's pass expression may match, so CTest's WILL_FAIL
            # inversion reports a failure rather than accepting the child error.
            output = ""
    sys.stdout.write(output)
    sys.stdout.flush()
    if missing:
        print(f"{FAILURE_MARKER} {name} requires labels: {', '.join(missing)}")
        return 1

    if will_fail and not has_pass_expression and result.returncode == 0:
        # Adding the audit pass expression must not make an unexpectedly
        # successful exit look like an expected failure.
        print(SUCCESS_MARKER)

    if os.name == "posix" and result.returncode < 0:
        # Preserve abnormal termination, which CTest must not invert for WILL_FAIL.
        sig = -result.returncode
        if sig not in (signal.SIGKILL, signal.SIGSTOP):
            signal.signal(sig, signal.SIG_DFL)
        os.kill(os.getpid(), sig)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
