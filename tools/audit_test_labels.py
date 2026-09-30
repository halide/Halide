#!/usr/bin/env python3
"""Audit the target_independent / llvm_independent CTest labels.

CI relies on these labels to avoid running the same test more than once:

  target_independent  The result does not depend on HL_TARGET / HL_JIT_TARGET,
                      so the test runs once per CI job instead of once per
                      Halide target.
  llvm_independent    The test never invokes LLVM code generation, so it runs
                      against only one LLVM version. Implies target_independent.

This script checks both labels empirically against an existing build, running
each test once:

  * A test is target independent if it still passes with HL_TARGET and
    HL_JIT_TARGET set to an unparsable string (any attempt to read the target
    from the environment then fails with "Did not understand Halide target").
  * A target-independent test is LLVM independent if, in that same run, it
    never emits the "llvm-entry" debug tag, which marks Halide loading its
    LLVM runtime bitcode. The tag is enabled alongside the default verbosity
    (HL_DEBUG_CODEGEN=0;tag:llvm-entry) so that debug(0) output, e.g.
    print_loop_nest(), still happens exactly as it does in CI.

Only JIT-style test executables are audited (see AUDITED_LABELS): AOT tests
(generator, tutorial, apps, ...) bake Halide_TARGET in at build time and so
must never be labeled target_independent, even though they don't read the
environment at run time.

Note: this is only proof for the host the audit runs on. A test that reads the
target only on some hosts would slip through, so keep an eye on tests that
branch on get_host_target().

Usage:
    tools/audit_test_labels.py --build-dir build [-C RelWithDebInfo] [-j N]
"""

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys

POISON = "audit-poisoned-target"
TARGET_ERROR = "Did not understand Halide target"
# Every LLVM code generation path loads runtime bitcode through
# parse_bitcode_file() in src/LLVM_Runtime_Linker.cpp, which logs LLVM_MARKER
# under this tag.
LLVM_DEBUG_RULES = "0;tag:llvm-entry"
LLVM_MARKER = "Loading runtime bitcode: "

# Tests with any of these labels are JIT tests built from plain C++ sources, so
# the environment is the only way their target can be chosen.
AUDITED_LABELS = {"correctness", "fuzz", "warning", "runtime_internal"}
# Autoscheduler unit tests that exercise the autoscheduler's internals directly.
AUDITED_NAMES = re.compile(
    r"^(test_perfect_hash_map"
    r"|(adams2019|anderson2021)_test_(function_dag|parser|state|storage_strides"
    r"|thread_info|tiling|bounds))$"
)


def ctest_json(build_dir, config):
    cmd = ["ctest", "--test-dir", build_dir, "--show-only=json-v1"]
    if config:
        cmd += ["-C", config]
    return json.loads(subprocess.check_output(cmd))


def props(test):
    return {p["name"]: p["value"] for p in test.get("properties", [])}


def in_scope(test):
    p = props(test)
    labels = set(p.get("LABELS", []))
    return bool(labels & AUDITED_LABELS) or bool(AUDITED_NAMES.match(test["name"]))


def run(test, extra_env, timeout):
    p = props(test)
    env = dict(os.environ)
    for kv in p.get("ENVIRONMENT", []):
        k, _, v = kv.partition("=")
        env[k] = v
    env.update(extra_env)
    try:
        r = subprocess.run(
            test["command"],
            cwd=p.get("WORKING_DIRECTORY") or None,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return None, ""
    return r.returncode, r.stdout.decode(errors="replace")


def passed(test, code, out):
    """Replicate CTest's pass/fail/skip logic for the properties Halide uses."""
    if code is None:
        return "timeout"
    p = props(test)
    skip = p.get("SKIP_REGULAR_EXPRESSION", [])
    if any(re.search(s, out) for s in skip):
        return "skipped"
    regexes = p.get("PASS_REGULAR_EXPRESSION", [])
    ok = any(re.search(s, out) for s in regexes) if regexes else code == 0
    if p.get("WILL_FAIL"):
        ok = not ok
    return "passed" if ok else "failed"


def audit(test, timeout):
    env = {
        "HL_TARGET": POISON,
        "HL_JIT_TARGET": POISON,
        "HL_DEBUG_CODEGEN": LLVM_DEBUG_RULES,
    }
    code, out = run(test, env, timeout)
    used_llvm = LLVM_MARKER in out
    status = passed(test, code, out)
    if status != "passed" or TARGET_ERROR in out or POISON in out:
        return test["name"], False, False, status
    return test["name"], True, not used_llvm, "passed"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("-C", "--config", default=None)
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count())
    ap.add_argument("-R", "--regex", default=None, help="only audit matching tests")
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument(
        "--strict",
        action="store_true",
        help="also fail when an unlabeled test could carry a label",
    )
    args = ap.parse_args()

    tests = [t for t in ctest_json(args.build_dir, args.config)["tests"] if in_scope(t)]
    if args.regex:
        tests = [t for t in tests if re.search(args.regex, t["name"])]
    labels = {t["name"]: set(props(t).get("LABELS", [])) for t in tests}

    print(f"Auditing {len(tests)} tests with {args.jobs} jobs...", file=sys.stderr)
    results = {}
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        futures = [ex.submit(audit, t, args.timeout) for t in tests]
        for i, f in enumerate(concurrent.futures.as_completed(futures), 1):
            name, ti, li, status = f.result()
            results[name] = (ti, li, status)
            print(
                f"[{i}/{len(tests)}] {name}: {status}"
                f"{' target_independent' if ti else ''}"
                f"{' llvm_independent' if li else ''}",
                file=sys.stderr,
            )

    errors, suggestions = [], []
    for name in sorted(results):
        ti, li, status = results[name]
        have = labels[name]
        if "target_independent" in have and not ti:
            errors.append(f"{name}: labeled target_independent but is not ({status})")
        if "llvm_independent" in have and not li:
            errors.append(f"{name}: labeled llvm_independent but is not")
        if "llvm_independent" in have and "target_independent" not in have:
            errors.append(f"{name}: llvm_independent requires target_independent")
        if ti and "target_independent" not in have:
            suggestions.append(f"{name}: could be labeled target_independent")
        if li and "llvm_independent" not in have:
            suggestions.append(f"{name}: could be labeled llvm_independent")

    for s in suggestions:
        print("note:", s)
    for e in errors:
        print("error:", e)
    return 1 if errors or (args.strict and suggestions) else 0


if __name__ == "__main__":
    sys.exit(main())
