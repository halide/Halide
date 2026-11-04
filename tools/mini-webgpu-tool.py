#!/usr/bin/env python3
"""Manage Halide's vendored WebGPU header, src/runtime/mini_webgpu.h.

Why this exists
--------------
mini_webgpu.h is a copy of the Dawn-generated C header <webgpu/webgpu.h>, with
exactly two Halide-specific modifications:

  1. the <stdint.h>/<stddef.h>/<math.h> includes are replaced by a
     "BEGIN/END Halide-specific changes" block (the Halide runtime can't include
     libc headers), and
  2. a HALIDE_MINI_WEBGPU_DAWN_VERSION marker recording which Dawn release the
     copy came from.

Dawn's C ABI is *not* append-only: it has inserted fields into the middle of
descriptors (e.g. signaledValueCount in WGPUShared*Memory*Access descriptors). So
the vendored header must match the linked libwebgpu_dawn exactly, not roughly --
any other combination is silent memory corruption in whichever descriptors cross
the boundary. The pin in vcpkg.json and the header therefore have to move
together, and `check` is what enforces that in CI.

Subcommands
-----------
  check [build_dir]      Verify consistency. Default build_dir: "build".
                         Always checks the marker-vs-pin rule (needs no build tree,
                         so it runs before Configure in CI); when the build tree is
                         present it additionally compares struct layouts, enum values
                         and declarations against the Dawn-generated header, and
                         checks the libwebgpu_dawn export table for every entry
                         point Halide calls. Exit 0 = consistent, 1 = drift.
  sync [dawn_header]     Rewrite mini_webgpu.h from a Dawn-generated webgpu.h,
                         preserving the Halide-specific block and stamping the
                         current vcpkg.json pin into the marker. Accepts a glob so
                         the triplet need not be spelled out, and defaults to
                         searching common build-dir layouts. Idempotent, and runs
                         `check` afterwards.
  selftest               Exercise check's pass/drift/broken paths and sync's
                         idempotency against throwaway fixtures in temp dirs. Never
                         touches the real tree. Degrades to the marker-only cases
                         when no Dawn-generated header is available.

The Dawn-generated header sync takes is the one vcpkg installs for the pinned
version, e.g.

  build/vcpkg_installed/arm64-osx-halide/include/dawn/webgpu.h

(the sibling include/webgpu/webgpu.h is only a 33-line forwarder to it).

Typical bump of the Dawn pin:
  1. edit the `dawn` override in vcpkg.json
  2. cmake --preset ... -DHalide_TARGET=host-webgpu   # rebuilds Dawn
  3. python3 tools/mini-webgpu-tool.py sync            # finds the generated header
  4. ctest --test-dir build -L correctness
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MINI_REL = "src/runtime/mini_webgpu.h"
MINI = os.path.join(REPO, MINI_REL)

INCLUDES = ("#include <stdint.h>", "#include <stddef.h>", "#include <math.h>")
BEGIN = "// BEGIN Halide-specific changes"
END = "// END Halide-specific changes"
MARKER = "#define HALIDE_MINI_WEBGPU_DAWN_VERSION"

# Halide entry points, not WebGPU API: names matching this are excluded from the
# "does mini_webgpu.h declare everything Halide calls" check.
LOCAL_HELPERS = ("wgpu_sv_to_cstr",)

# Files that call into the WebGPU API.
SCAN = ["src/runtime/webgpu.cpp", "test/common/gpu_context.h"]


def dawn_pin():
    """The `dawn` version pinned in vcpkg.json's overrides."""
    manifest = json.load(open(os.path.join(REPO, "vcpkg.json")))
    for o in manifest.get("overrides", []):
        if o["name"] == "dawn":
            return o["version"]
    raise SystemExit("no `dawn` override in vcpkg.json")


# Default fixtures for `selftest` / `sync`: any build dir with a configured WebGPU
# build, over the usual triplet and library suffix candidates.
GEN_GLOBS = [
    "build*/vcpkg_installed/*/include/dawn/webgpu.h",
    "build*/vcpkg-install*/include/dawn/webgpu.h",
]
LIB_GLOBS = [
    "build*/vcpkg_installed/*/lib/libwebgpu_dawn.dylib",
    "build*/vcpkg_installed/*/lib/libwebgpu_dawn.so",
    "build*/vcpkg-install*/lib/libwebgpu_dawn.dylib",
    "build*/vcpkg-install*/lib/libwebgpu_dawn.so",
]


def resolve_one(pattern, required=True):
    """Resolve a path-or-glob (or list of them) to exactly one file, preferring
    non-debug artifacts. Returns None when nothing matches and not required."""
    pats = pattern if isinstance(pattern, (list, tuple)) else [pattern]
    hits = []
    for p in pats:
        hits += [h for h in glob.glob(os.path.join(REPO, p)) if "debug" not in h]
    hits = sorted(set(hits))
    if not hits:
        if required:
            raise SystemExit("no file matched %r; pass an explicit path" % (pats,))
        return None
    if len(hits) > 1:
        raise SystemExit("ambiguous: %d files matched %r:\n  %s\n"
                        "pass a more specific path" % (len(hits), pats, "\n  ".join(hits)))
    return hits[0]


def entries(path):
    """Parse a C WebGPU header into (struct name -> field names,
    enumerator name -> value, set of declared wgpu* entry points)."""
    s = open(path).read()
    structs = {m.group(1): [f.strip().split()[-1]
                           for f in m.group(2).split(';') if f.strip()]
               for m in re.finditer(
                   r'typedef struct (\w+)(?::\s*\w+)?\s*\{(.*?)\}\s*\1(?:\s+\w+)?\s*;',
                   s, re.S)}
    enums = {}
    for m in re.finditer(r'typedef enum(?:\s+\w+)?\s*\{(.*?)\}\s*\w+(?:\s+\w+)?\s*;', s, re.S):
        for line in m.group(1).split(','):
            mm = re.match(r'\s*(WGPU\w+)\s*=\s*(-?\d+)', line)
            if mm:
                enums[mm.group(1)] = int(mm.group(2))
    funcs = set(re.findall(r'\b(wgpu[A-Za-z0-9_]+)\s*\(', s))
    return structs, enums, funcs


def halide_calls(root=REPO):
    """wgpu* names referenced by the Halide runtime and test helper."""
    used = set()
    for p in SCAN:
        used |= set(re.findall(r'\b(wgpu[A-Za-z0-9_]+)\s*\(',
                              open(os.path.join(root, p)).read()))
    return {f for f in used if not any(f.startswith(h) for h in LOCAL_HELPERS)}


# --------------------------------------------------------------------------- check

def check(root=REPO, build="build"):
    """Return (exit code, output lines). See the `check` subcommand."""
    out = []
    rc = 0

    try:
        pin = dawn_pin()
    except SystemExit as e:
        return 1, ["FAIL: %s" % e]
    out.append("vcpkg.json pins dawn %s" % pin)

    mini = open(os.path.join(root, MINI_REL)).read()
    marked = re.search(re.escape(MARKER) + r' "([^"]+)"', mini)
    if not marked:
        msg = "FAIL: %s not found in %s" % (MARKER, MINI_REL)
        return 1, out + [msg]
    if marked.group(1) != pin:
        return 1, out + [
            "FAIL: %s says Dawn %s, vcpkg.json pins %s." % (MINI_REL, marked.group(1), pin),
            "      Re-sync with: python3 tools/mini-webgpu-tool.py sync "
            "<build>/vcpkg_installed/<triplet>/include/dawn/webgpu.h"]
    out.append("marker matches pin: %s" % marked.group(1))

    gen = glob.glob(os.path.join(root, build, "vcpkg_installed", "*", "include",
                                "dawn", "webgpu.h"))
    libs = [p for p in glob.glob(os.path.join(root, build, "vcpkg_installed", "*", "lib",
                                              "libwebgpu_dawn.*")) if "debug" not in p]
    if not gen:
        out.append("NOTE: no Dawn-generated webgpu.h under %s/ (not configured yet); "
                  "skipping struct/enum/decl comparison" % build)
        return rc, out

    ms, me, mf = entries(os.path.join(root, MINI_REL))
    ds, de, df = entries(gen[0])
    out.append("mini: %d structs, %d enumerators, %d entry points" %
               (len(ms), len(me), len(mf)))

    bad = [n for n in ms if n in ds and ms[n] != ds[n]]
    out.append("struct layout mismatches: %d" % len(bad))
    for n in bad:
        rc = 1
        out.append("  ! %s\n      mini : %s\n      dawn : %s" % (n, ms[n], ds[n]))

    ev = [(k, me[k], de[k]) for k in me if k in de and me[k] != de[k]]
    out.append("enum value mismatches: %d" % len(ev))
    if ev:
        rc = 1
        out.append("  ! %s" % ev[:10])

    lost = sorted(df - mf)
    out.append("declared upstream but missing from mini: %d" % len(lost))
    if lost:
        rc = 1
        out.append("  ! %s" % lost[:10])
    extra = sorted(mf - df)
    out.append("declared by mini but not upstream: %d" % len(extra))
    if extra:
        rc = 1
        out.append("  ! %s" % extra[:10])

    used = halide_calls(root) & df
    undeclared = sorted(used - mf)
    out.append("entry points used by Halide but not declared by mini: %d" % len(undeclared))
    if undeclared:
        rc = 1
        out.append("  ! %s" % undeclared)

    if libs:
        proc = subprocess.run(["nm", "-gU", os.path.join(root, libs[0])],
                             capture_output=True, text=True)
        exported = {l.split()[-1] for l in proc.stdout.splitlines() if l.strip()}
        missing = sorted(f for f in used if "_%s" % f not in exported)
        out.append("Halide-called entry points absent from %s: %d" %
                   (os.path.basename(libs[0]), len(missing)))
        if missing:
            rc = 1
            out.append("  ! %s" % missing)
    else:
        out.append("NOTE: no libwebgpu_dawn under %s/; skipped the export-table check" % build)

    return rc, out


def cmd_check(args):
    rc, out = check(build=args.build_dir)
    print("\n".join(out))
    print("PASS" if rc == 0 else "FAIL")
    return rc


# ---------------------------------------------------------------------------- sync

def halide_block(lines, pin):
    """The BEGIN/END Halide-specific block from the existing file, normalized: any
    previously stamped version marker removed, blank runs collapsed, and a fresh
    marker appended just before END. This makes sync idempotent."""
    out, keep = [], False
    for line in lines:
        if line.strip() == BEGIN:
            keep = True
        if keep:
            out.append(line)
        if line.strip() == END:
            keep = False
    if not out:
        raise SystemExit("Halide-specific block not found in %s" % MINI_REL)
    junk = ("The Dawn release this copy", "dawn override in vcpkg.json")
    body = []
    for l in out:
        s = l.strip()
        if s in (BEGIN, END):                 # re-added verbatim below
            continue
        if l.startswith(MARKER) or l.lstrip("#/ ").startswith(junk):
            continue                          # a previously stamped version marker
        if not s and body and not body[-1].strip():
            continue                          # collapse runs of blank lines
        body.append(l)
    while body and not body[-1].strip():
        body.pop()
    return [BEGIN] + body + ["",
                             "// The Dawn release this copy was synced from. CI asserts this "
                             "matches the",
                             "// dawn override in vcpkg.json (see tools/mini-webgpu-tool.py check).",
                             '%s "%s"' % (MARKER, pin),
                             "", END]


def render(dawn_header_path, mini_text, pin):
    """Produce the vendored header text for a Dawn-generated header."""
    dawn = open(dawn_header_path).read().splitlines()
    # Guard against the 33-line forwarder shipped at include/webgpu/webgpu.h.
    if len(dawn) < 1000:
        raise SystemExit("source header has %d lines; that's the forwarder, not the "
                         "Dawn-generated dawn/webgpu.h" % len(dawn))

    idx = [i for i, l in enumerate(dawn) if l.strip() in INCLUDES]
    if len(idx) != 3 or idx != list(range(idx[0], idx[0] + 3)):
        raise SystemExit("expected the three libc includes together in the Dawn header")

    head, tail = dawn[:idx[0]], dawn[idx[0] + 3:]
    while head and not head[-1].strip():
        head.pop()
    while tail and not tail[0].strip():
        tail.pop(0)
    text = "\n".join(head + [""] + halide_block(mini_text.splitlines(), pin) + [""] + tail) + "\n"

    for line in text.splitlines():
        if line.strip().startswith("#include"):
            raise SystemExit("libc include leaked into output: %s" % line)
    if text.count(MARKER) != 1:
        raise SystemExit("expected exactly one version marker, found %d" % text.count(MARKER))
    return text


def cmd_sync(args):
    pin = dawn_pin()
    src = resolve_one(args.dawn_header or GEN_GLOBS)
    new = render(src, open(MINI).read(), pin)

    if new == open(MINI).read():
        print("%s already matches Dawn %s; nothing to do" % (MINI_REL, pin))
        return 0
    open(MINI, "w").write(new)
    print("wrote %s (%d lines) from Dawn %s" % (MINI_REL, new.count("\n"), pin))
    if args.build_dir:
        build = args.build_dir
    else:
        # <build>/vcpkg_installed/<triplet>/include/dawn/webgpu.h -> <build>
        d = src
        for _ in range(5):
            d = os.path.dirname(d)
        build = os.path.relpath(d, REPO)
    rc, out = check(build=build)
    print("\n".join(out))
    print("PASS" if rc == 0 else "FAIL")
    return rc


# ------------------------------------------------------------------------ selftest

# (name, marker version or None to delete it, corrupt anchor, expected exit)
CASES = [
    ("pass", "20260624.223603", None, 0),
    ("drift", "20260911.162847", None, 1),
    ("no marker", None, None, 1),
    ("layout", "20260624.223603", "WGPUBool initialized;", 1),
]


def selftest(gen, lib):
    """Run `check` against throwaway fixtures. Returns exit code.

    gen/lib are the real Dawn-generated header and built libwebgpu_dawn used as the
    fixtures' upstream (lib may be None). check() resolves paths from its own
    arguments, so the fixture must contain its own copy of this script; otherwise
    the real repo's files would be audited and every deliberately-broken case would
    pass.
    """
    results = []

    def run(tmp, build):
        p = subprocess.run([sys.executable, os.path.join(tmp, "tools", "mini-webgpu-tool.py"),
                           "check", build], capture_output=True, text=True)
        return p.returncode, p.stdout + p.stderr

    def fixture(tmp, marker_ver, corrupt, gen=None, lib=None):
        for d in ("src/runtime", "test/common", "tools"):
            os.makedirs(os.path.join(tmp, d), exist_ok=True)
        for f in (MINI_REL, "src/runtime/webgpu.cpp", "test/common/gpu_context.h",
                  "vcpkg.json"):
            shutil.copy(os.path.join(REPO, f), os.path.join(tmp, f))
        shutil.copy(os.path.abspath(__file__), os.path.join(tmp, "tools", "mini-webgpu-tool.py"))
        if gen:
            gen_dir = os.path.join(tmp, "build/vcpkg_installed/arm64-osx-halide/include/dawn")
            os.makedirs(gen_dir)
            shutil.copy(gen, os.path.join(gen_dir, "webgpu.h"))

            lib_dir = os.path.join(tmp, "build/vcpkg_installed/arm64-osx-halide/lib")
            os.makedirs(lib_dir)
            shutil.copy(lib, os.path.join(lib_dir, os.path.basename(lib)))

        mini = os.path.join(tmp, MINI_REL)
        text = open(mini).read()
        old = '%s "20260624.223603"' % MARKER
        assert old in text, "sample marker not found in %s" % MINI_REL
        if marker_ver:
            text = text.replace(old, '%s "%s"' % (MARKER, marker_ver))
        else:
            text = text.replace(old + "\n", "")
        if corrupt:                      # simulate a field Dawn inserted mid-struct
            text = text.replace(corrupt, corrupt + "\n    size_t insertedField;", 1)
        open(mini, "w").write(text)

    for name, ver, corrupt, want in CASES:
        tmp = tempfile.mkdtemp(prefix="miniwebgpu_")
        try:
            fixture(tmp, ver, corrupt, gen, lib)
            got, out = run(tmp, "build")
            key = [l for l in out.splitlines() if l.startswith(("FAIL", "PASS", "struct layout"))]
            results.append((name, want, got, got == want, key[:2]))
        finally:
            shutil.rmtree(tmp)

    # `check` before Configure: marker rule must still pass with no build tree.
    tmp = tempfile.mkdtemp(prefix="miniwebgpu_notree_")
    try:
        fixture(tmp, "20260624.223603", None)
        got, out = run(tmp, "build")
        key = [l for l in out.splitlines() if l.startswith("NOTE")]
        results.append(("no build tree", 0, got, got == 0, key[:1]))
    finally:
        shutil.rmtree(tmp)

    # sync must be idempotent: rendering twice yields identical text.
    if gen:
        try:
            pin = dawn_pin()
            first = render(gen, open(MINI).read(), pin)
            second = render(gen, first, pin)
            ok = first == second
            results.append(("sync idempotent", True, ok, ok, ["render(render(x)) == render(x)"]))
        except SystemExit as e:
            results.append(("sync idempotent", True, str(e), False, []))
    else:
        results.append(("sync idempotent", "skip", "skipped", True,
                       ["no Dawn-generated header available"]))

    rc = 0
    for name, want, got, ok, detail in results:
        if not ok:
            rc = 1
        print("%-14s expect %s, got %s  %s  %s" %
              (name, want, got, "OK" if ok else "*** MISMATCH ***", detail))
    print("PASS" if rc == 0 else "FAIL")
    return rc


def cmd_selftest(args):
    gen = resolve_one(args.dawn_header, required=False)
    lib = resolve_one(args.lib, required=False) if gen else None
    if not gen:
        print("NOTE: no Dawn-generated webgpu.h found; running the reduced selftest "
              "(marker rules only). Pass --dawn-header <path> to exercise the "
              "struct/enum/symbol paths.")
    return selftest(gen, lib)


# ---------------------------------------------------------------------------- main



def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("check", help="verify mini_webgpu.h matches the Dawn pin "
                                    "(and the build tree, if present)")
    c.add_argument("build_dir", nargs="?", default="build",
                   help="CMake build dir to find vcpkg_installed in (default: build)")
    c.set_defaults(func=cmd_check)

    s = sub.add_parser("sync", help="rewrite mini_webgpu.h from a Dawn-generated header")
    s.add_argument("dawn_header", nargs="?", default=None,
                   help="path or glob for the Dawn-generated webgpu.h, e.g. "
                        "build/vcpkg_installed/*/include/dawn/webgpu.h "
                        "(default: search common build-dir layouts)")
    s.add_argument("--build-dir", default=None,
                   help="build dir to verify against (default: derive from the header path)")
    s.set_defaults(func=cmd_sync)

    t = sub.add_parser("selftest", help="exercise check's pass/drift/broken paths "
                                       "against temp fixtures")
    t.add_argument("--dawn-header", default=GEN_GLOBS,
                   help="Dawn-generated header (path or glob) for the fixture's upstream")
    t.add_argument("--lib", default=LIB_GLOBS,
                  help="built libwebgpu_dawn (path or glob) for the export-table fixture")
    t.set_defaults(func=cmd_selftest)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
