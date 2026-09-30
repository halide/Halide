#!/usr/bin/env python3
"""Audit the target_independent / llvm_independent / gpu CTest labels.

CI relies on these labels to avoid running the same test more than once:

  target_independent  The result does not depend on HL_TARGET / HL_JIT_TARGET,
                      so the test runs once per CI job instead of once per
                      Halide target.
  llvm_independent    The test never invokes LLVM code generation, so it runs
                      against only one LLVM version. Implies target_independent.
  gpu                 The test exercises the GPU when the target has a GPU
                      feature, so it runs in the CI steps that test a GPU
                      target.

This script checks them empirically against an existing build. Each test runs
once, with HL_TARGET / HL_JIT_TARGET set to --target (default host) and
HL_DEBUG_CODEGEN=0;tag:target-env,llvm-entry,gpu-entry. Keeping verbosity 0
means debug(0) output, e.g. print_loop_nest(), happens exactly as in CI. Each
tag marks one thing in the test's output:

  target-env  Halide read the target from the environment
              (get_target_from_environment / get_jit_target_from_environment).
              A passing test that never does this is target independent.
  llvm-entry  Halide loaded its LLVM runtime bitcode, which every LLVM code
              generation path does. A target-independent test that never
              does this is LLVM independent.
  gpu-entry   Halide compiled a GPU kernel or fetched a device interface. When
              --target has a GPU feature (e.g. host-metal), every test that
              does this and reads the target must be labeled gpu. (A
              target-independent test uses the GPU the same way everywhere, so
              it needs no gpu label.) A gpu-labeled test that doesn't use the
              GPU, e.g. a CUDA-only test that skips itself on Metal, or one
              checking a GPU-only schedule error, is reported, not rejected.

Only JIT-style test executables are audited (see AUDITED_LABELS): AOT tests
(generator, tutorial, apps, ...) bake Halide_TARGET in at build time and so
must never be labeled target_independent, even though they don't read the
environment at run time.

Note: this is only proof for the host the audit runs on. A test that reads the
target only on some hosts would slip through, so keep an eye on tests that
branch on get_host_target().

Usage:
    tools/audit_test_labels.py --build-dir build [-C RelWithDebInfo] [-j N]
                               [--target host-metal]
"""

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys

DEBUG_RULES = "0;tag:target-env,llvm-entry,gpu-entry"
# Logged under target-env by src/Target.cpp.
TARGET_MARKER = "Reading target from environment: "
# Logged under llvm-entry by src/LLVM_Runtime_Linker.cpp.
LLVM_MARKER = "Loading runtime bitcode: "
# Logged under gpu-entry by src/OffloadGPULoops.cpp and src/DeviceInterface.cpp.
GPU_MARKERS = ("Compiling GPU kernel: ", "Using device interface: ")
GPU_FEATURES = {"cuda", "opencl", "metal", "vulkan", "d3d12compute", "webgpu"}

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


def audit(test, timeout, target):
    env = {
        "HL_TARGET": target,
        "HL_JIT_TARGET": target,
        "HL_DEBUG_CODEGEN": DEBUG_RULES,
    }
    code, out = run(test, env, timeout)
    status = passed(test, code, out)
    ti = status == "passed" and TARGET_MARKER not in out
    return test["name"], {
        "status": status,
        "ti": ti,
        "li": ti and LLVM_MARKER not in out,
        "gpu": any(m in out for m in GPU_MARKERS),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("-C", "--config", default=None)
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count())
    ap.add_argument("-R", "--regex", default=None, help="only audit matching tests")
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument(
        "--target",
        default="host",
        help="HL_TARGET for the audit run; give one with a GPU feature the host"
        " supports (e.g. host-metal) to also audit the gpu label",
    )
    ap.add_argument(
        "--strict",
        action="store_true",
        help="also fail when an unlabeled test could carry a label",
    )
    args = ap.parse_args()

    check_gpu = bool(GPU_FEATURES & set(args.target.split("-")))
    tests = [t for t in ctest_json(args.build_dir, args.config)["tests"] if in_scope(t)]
    if args.regex:
        tests = [t for t in tests if re.search(args.regex, t["name"])]
    labels = {t["name"]: set(props(t).get("LABELS", [])) for t in tests}

    print(f"Auditing {len(tests)} tests with {args.jobs} jobs...", file=sys.stderr)
    results = {}
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        futures = [ex.submit(audit, t, args.timeout, args.target) for t in tests]
        for i, f in enumerate(concurrent.futures.as_completed(futures), 1):
            name, r = f.result()
            results[name] = r
            print(
                f"[{i}/{len(tests)}] {name}: {r['status']}"
                f"{' target_independent' if r['ti'] else ''}"
                f"{' llvm_independent' if r['li'] else ''}"
                f"{' gpu' if r['gpu'] else ''}",
                file=sys.stderr,
            )

    errors, suggestions, notes = [], [], []
    for name in sorted(results):
        r = results[name]
        ti, li, status = r["ti"], r["li"], r["status"]
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
        if check_gpu:
            if r["gpu"] and not ti and "gpu" not in have:
                errors.append(f"{name}: uses the GPU but is not labeled gpu")
            if ti and "gpu" in have:
                errors.append(
                    f"{name}: gpu label is redundant on a target-independent test"
                )
            if "gpu" in have and not r["gpu"]:
                notes.append(f"{name}: labeled gpu but did not use the GPU")
            if status != "passed":
                notes.append(f"{name}: {status} on {args.target}")

    for n in notes:
        print("info:", n)
    for s in suggestions:
        print("note:", s)
    for e in errors:
        print("error:", e)
    return 1 if errors or (args.strict and suggestions) else 0


if __name__ == "__main__":
    sys.exit(main())
