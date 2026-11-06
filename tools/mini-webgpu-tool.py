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
  pinstatus              Report whether a newer Dawn is available than the
                         vcpkg.json pin, using the vcpkg instance the build would
                         use (on CI runners, the image's own snapshot). Advisory
                         only: exit 0 whatever it concludes, unless --strict is
                         given. Also warns when the pin is NEWER than that
                         snapshot, which is the case that breaks CI resolution.

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
import urllib.error
import urllib.request

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


def pin_info():
    """(version, port-version) for the `dawn` override in vcpkg.json."""
    with open(os.path.join(REPO, "vcpkg.json")) as fh:
        manifest = json.load(fh)
    for o in manifest.get("overrides", []):
        if o["name"] == "dawn":
            return o["version"], int(o.get("port-version", 0))
    raise SystemExit("no `dawn` override in vcpkg.json")


def dawn_pin():
    """The `dawn` version pinned in vcpkg.json's overrides."""
    return pin_info()[0]


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
            raise SystemExit(f"no file matched {pats!r}; pass an explicit path")
        return None
    if len(hits) > 1:
        joined = "\n  ".join(hits)
        raise SystemExit(
            f"ambiguous: {len(hits)} files matched {pats!r}:\n  {joined}\n"
            "pass a more specific path"
        )
    return hits[0]


def entries(path):
    """Parse a C WebGPU header into (struct name -> field names,
    enumerator name -> value, set of declared wgpu* entry points)."""
    with open(path) as fh:
        s = fh.read()
    structs = {
        m.group(1): [f.strip().split()[-1] for f in m.group(2).split(";") if f.strip()]
        for m in re.finditer(
            r"typedef struct (\w+)(?::\s*\w+)?\s*\{(.*?)\}\s*\1(?:\s+\w+)?\s*;",
            s,
            re.DOTALL,
        )
    }
    enums = {}
    for m in re.finditer(
        r"typedef enum(?:\s+\w+)?\s*\{(.*?)\}\s*\w+(?:\s+\w+)?\s*;", s, re.DOTALL
    ):
        for line in m.group(1).split(","):
            mm = re.match(r"\s*(WGPU\w+)\s*=\s*(-?\d+)", line)
            if mm:
                enums[mm.group(1)] = int(mm.group(2))
    funcs = set(re.findall(r"\b(wgpu[A-Za-z0-9_]+)\s*\(", s))
    return structs, enums, funcs


def halide_calls(root=REPO):
    """wgpu* names referenced by the Halide runtime and test helper."""
    used = set()
    for p in SCAN:
        with open(os.path.join(root, p)) as fh:
            used |= set(re.findall(r"\b(wgpu[A-Za-z0-9_]+)\s*\(", fh.read()))
    return {f for f in used if not any(f.startswith(h) for h in LOCAL_HELPERS)}


# --------------------------------------------------------------------------- check


def check(root=REPO, build="build"):
    """Return (exit code, output lines). See the `check` subcommand."""
    out = []
    rc = 0

    try:
        pin = dawn_pin()
    except SystemExit as e:
        return 1, [f"FAIL: {e}"]
    out.append(f"vcpkg.json pins dawn {pin}")

    with open(os.path.join(root, MINI_REL)) as fh:
        mini = fh.read()
    marked = re.search(re.escape(MARKER) + r' "([^"]+)"', mini)
    if not marked:
        msg = f"FAIL: {MARKER} not found in {MINI_REL}"
        return 1, [*out, msg]
    if marked.group(1) != pin:
        resync = (
            "      Re-sync with: python3 tools/mini-webgpu-tool.py sync "
            "<build>/vcpkg_installed/<triplet>/include/dawn/webgpu.h"
        )
        return 1, [
            *out,
            f"FAIL: {MINI_REL} says Dawn {marked.group(1)}, vcpkg.json pins {pin}.",
            resync,
        ]
    out.append(f"marker matches pin: {marked.group(1)}")

    gen = glob.glob(
        os.path.join(root, build, "vcpkg_installed", "*", "include", "dawn", "webgpu.h")
    )
    libs = [
        p
        for p in glob.glob(
            os.path.join(root, build, "vcpkg_installed", "*", "lib", "libwebgpu_dawn.*")
        )
        if "debug" not in p
    ]
    if not gen:
        out.append(
            f"NOTE: no Dawn-generated webgpu.h under {build}/ (not configured yet); "
            "skipping struct/enum/decl comparison"
        )
        return rc, out

    ms, me, mf = entries(os.path.join(root, MINI_REL))
    ds, de, df = entries(gen[0])
    out.append(
        f"mini: {len(ms)} structs, {len(me)} enumerators, {len(mf)} entry points"
    )

    bad = [n for n in ms if n in ds and ms[n] != ds[n]]
    out.append(f"struct layout mismatches: {len(bad)}")
    for n in bad:
        rc = 1
        out.append(f"  ! {n}\n      mini : {ms[n]}\n      dawn : {ds[n]}")

    ev = [(k, me[k], de[k]) for k in me if k in de and me[k] != de[k]]
    out.append(f"enum value mismatches: {len(ev)}")
    if ev:
        rc = 1
        out.append(f"  ! {ev[:10]}")

    lost = sorted(df - mf)
    out.append(f"declared upstream but missing from mini: {len(lost)}")
    if lost:
        rc = 1
        out.append(f"  ! {lost[:10]}")
    extra = sorted(mf - df)
    out.append(f"declared by mini but not upstream: {len(extra)}")
    if extra:
        rc = 1
        out.append(f"  ! {extra[:10]}")

    used = halide_calls(root) & df
    undeclared = sorted(used - mf)
    out.append(
        f"entry points used by Halide but not declared by mini: {len(undeclared)}"
    )
    if undeclared:
        rc = 1
        out.append(f"  ! {undeclared}")

    if libs:
        proc = subprocess.run(
            ["nm", "-gU", os.path.join(root, libs[0])], capture_output=True, text=True
        )
        exported = {tok.split()[-1] for tok in proc.stdout.splitlines() if tok.strip()}
        missing = sorted(fn for fn in used if f"_{fn}" not in exported)
        out.append(
            f"Halide-called entry points absent from {os.path.basename(libs[0])}: "
            f"{len(missing)}"
        )
        if missing:
            rc = 1
            out.append(f"  ! {missing}")
    else:
        out.append(
            f"NOTE: no libwebgpu_dawn under {build}/; skipped the export-table check"
        )

    return rc, out


def cmd_check(args):
    rc, out = check(build=args.build_dir)
    print("\n".join(out))
    print("PASS" if rc == 0 else "FAIL")
    return rc


# ----------------------------------------------------------------- pin availability

# The newest `dawn` port on vcpkg master, for the --remote comparison.
MASTER_PORT_URL = (
    "https://raw.githubusercontent.com/microsoft/vcpkg/master/ports/dawn/vcpkg.json"
)


def vcpkg_roots(explicit=None):
    """Candidate vcpkg instances, most authoritative first.

    On GitHub-hosted runners VCPKG_INSTALLATION_ROOT is the checkout baked into
    the image, so its registry snapshot *is* what CI can install -- which is what
    constrains our pin, regardless of what a newer local vcpkg knows about.
    """
    cands = [explicit]
    cands += [os.environ.get(v) for v in ("VCPKG_INSTALLATION_ROOT", "VCPKG_ROOT")]
    exe = shutil.which("vcpkg")
    if exe:
        cands.append(os.path.dirname(os.path.realpath(exe)))
    return [c for c in cands if c and os.path.isdir(os.path.join(c, "ports", "dawn"))]


def port_json(path):
    """(version, port-version) from a port's vcpkg.json, or None if unreadable."""
    try:
        with open(path) as fh:
            j = json.load(fh)
    except (OSError, ValueError):
        return None
    v = j.get("version") or j.get("version-date") or j.get("version-semver")
    if not v:
        return None
    return str(v), int(j.get("port-version", 0))


def baseline(root):
    """(short sha, commit date) of a vcpkg checkout, or (None, None)."""
    try:
        proc = subprocess.run(
            ["git", "-C", root, "log", "-1", "--format=%H%x09%cd", "--date=short"],
            capture_output=True,
            text=True,
            timeout=15,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None, None
    if proc.returncode != 0 or not proc.stdout.strip():
        return None, None
    sha, _, date = proc.stdout.strip().partition("\t")
    return sha[:12], date


def version_key(v):
    """Order Dawn versions (date.build.portversion); None when unparseable."""
    if v is None:
        return None
    s, pv = v
    m = re.match(r"^(\d{8})(?:\.(\d+))?$", s)
    if not m:
        return (0, int(s), pv) if s.isdigit() else None
    return (int(m.group(1)), int(m.group(2) or 0), pv)


def remote_dawn(timeout=10):
    """(version, port-version) on vcpkg master, or (None, reason)."""
    try:
        req = urllib.request.Request(
            MASTER_PORT_URL, headers={"User-Agent": "halide-mini-webgpu-tool"}
        )
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            j = json.load(resp)
    except (urllib.error.URLError, OSError, ValueError, TimeoutError) as e:
        return None, f"{type(e).__name__}: {e}"
    v = j.get("version") or j.get("version-date") or j.get("version-semver")
    if not v:
        return None, "no version field in the master port manifest"
    return (str(v), int(j.get("port-version", 0))), None


def pinstatus(explicit=None, remote=False):
    """Compare the vcpkg.json Dawn pin with what a vcpkg instance can install.

    Returns (verdict, lines). The verdict is one of PIN-UP-TO-DATE,
    PIN-UPDATE-AVAILABLE, PIN-AHEAD-OF-REGISTRY or PIN-UNKNOWN; none of them is a
    failure, this is a heads-up about an available update, not a correctness gate
    (`check` is the gate).
    """
    out = []
    try:
        pin, pin_pv = pin_info()
    except SystemExit as e:
        return "PIN-UNKNOWN", [f"FAIL: {e}"]
    out.append(f"vcpkg.json pins dawn {pin}#{pin_pv}")

    roots = vcpkg_roots(explicit)
    if not roots:
        return "PIN-UNKNOWN", [
            *out,
            "NOTE: no vcpkg instance found (set VCPKG_INSTALLATION_ROOT or VCPKG_ROOT, "
            "or pass --vcpkg-root); cannot say what is available",
        ]
    root = roots[0]
    sha, date = baseline(root)
    where = f"{root}" + (f" @ {sha} ({date})" if sha else " (baseline sha unknown)")
    out.append(f"asking vcpkg {where}")
    if len(roots) > 1:
        out.append(f"  (also visible: {', '.join(roots[1:])})")

    avail = port_json(os.path.join(root, "ports", "dawn", "vcpkg.json"))
    if avail is None:
        return "PIN-UNKNOWN", [*out, "NOTE: could not read ports/dawn/vcpkg.json"]
    out.append(f"that snapshot offers dawn {avail[0]}#{avail[1]}")

    kpin, kavail = version_key((pin, pin_pv)), version_key(avail)
    if kpin is None or kavail is None:
        return "PIN-UNKNOWN", [
            *out,
            "NOTE: unparseable version string; skipping the comparison",
        ]

    verdict = "PIN-UP-TO-DATE"
    if kavail > kpin:
        verdict = "PIN-UPDATE-AVAILABLE"
        out.append(
            f"UPDATE AVAILABLE: {avail[0]}#{avail[1]} is newer than the pin. Bump the "
            "`dawn` override in vcpkg.json, then run sync + check so the vendored "
            "header moves with it (struct layouts can change mid-descriptor)."
        )
    elif kavail < kpin:
        verdict = "PIN-AHEAD-OF-REGISTRY"
        out.append(
            f"WARNING: the pin is newer than this vcpkg snapshot ({avail[0]}#{avail[1]}), "
            "so a checkout of this registry cannot install it. CI images with the same "
            "baseline will fail to resolve the manifest unless they use a newer "
            "baseline or VCPKG_BASELINE."
        )
    else:
        out.append("pin matches what this vcpkg offers")

    if remote:
        latest, why = remote_dawn()
        if latest is None:
            out.append(
                f"NOTE: vcpkg master lookup failed ({why}); report is local-only"
            )
        else:
            out.append(f"vcpkg master currently has dawn {latest[0]}#{latest[1]}")
            kmaster = version_key(latest)
            if kmaster and kmaster > max(kpin, kavail):
                out.append(
                    f"NOTE: master is newer than both the pin and this snapshot; "
                    f"updating the local vcpkg checkout (or the runner image) would "
                    f"expose {latest[0]}#{latest[1]}"
                )

    return verdict, out


def cmd_pinstatus(args):
    verdict, out = pinstatus(explicit=args.vcpkg_root, remote=args.remote)
    print("\n".join(out))
    print(verdict)
    # Advisory by default: an available update must never break CI. --strict lets
    # a maintainer cron job or local shell ask for a nonzero exit instead.
    if args.strict and verdict == "PIN-UPDATE-AVAILABLE":
        return 1
    return 0


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
        raise SystemExit(f"Halide-specific block not found in {MINI_REL}")
    junk = ("The Dawn release this copy", "dawn override in vcpkg.json")
    body = []
    for line in out:
        s = line.strip()
        if s in (BEGIN, END):  # re-added verbatim below
            continue
        if line.startswith(MARKER) or line.lstrip("#/ ").startswith(junk):
            continue  # a previously stamped version marker
        if not s and body and not body[-1].strip():
            continue  # collapse runs of blank lines
        body.append(line)
    while body and not body[-1].strip():
        body.pop()
    stamp = [
        "// The Dawn release this copy was synced from. CI asserts this matches the",
        "// dawn override in vcpkg.json (see tools/mini-webgpu-tool.py check).",
    ]
    return [BEGIN, *body, "", *stamp, f'{MARKER} "{pin}"', "", END]


def render(dawn_header_path, mini_text, pin):
    """Produce the vendored header text for a Dawn-generated header."""
    with open(dawn_header_path) as fh:
        dawn = fh.read().splitlines()
    # Guard against the 33-line forwarder shipped at include/webgpu/webgpu.h.
    if len(dawn) < 1000:
        raise SystemExit(
            f"source header has {len(dawn)} lines; that's the forwarder, not the "
            "Dawn-generated dawn/webgpu.h"
        )

    idx = [i for i, line in enumerate(dawn) if line.strip() in INCLUDES]
    if len(idx) != 3 or idx != list(range(idx[0], idx[0] + 3)):
        raise SystemExit("expected the three libc includes together in the Dawn header")

    head, tail = dawn[: idx[0]], dawn[idx[0] + 3 :]
    while head and not head[-1].strip():
        head.pop()
    while tail and not tail[0].strip():
        tail.pop(0)
    text = (
        "\n".join([*head, "", *halide_block(mini_text.splitlines(), pin), "", *tail])
        + "\n"
    )

    for line in text.splitlines():
        if line.strip().startswith("#include"):
            raise SystemExit(f"libc include leaked into output: {line}")
    if text.count(MARKER) != 1:
        raise SystemExit(
            f"expected exactly one version marker, found {text.count(MARKER)}"
        )
    return text


def cmd_sync(args):
    pin = dawn_pin()
    src = resolve_one(args.dawn_header or GEN_GLOBS)
    with open(MINI) as fh:
        old = fh.read()
    new = render(src, old, pin)

    if new == old:
        print(f"{MINI_REL} already matches Dawn {pin}; nothing to do")
        return 0
    with open(MINI, "w") as fh:
        fh.write(new)
    nlines = new.count("\n")
    print(f"wrote {MINI_REL} ({nlines} lines) from Dawn {pin}")
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
# Cases with a corrupt anchor need a Dawn-generated header as the comparison
# upstream, so they are skipped (not failed) when none is available.
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

    def run(tmp, argv):
        p = subprocess.run(
            [sys.executable, os.path.join(tmp, "tools", "mini-webgpu-tool.py"), *argv],
            capture_output=True,
            text=True,
        )
        return p.returncode, p.stdout + p.stderr

    def fixture(tmp, marker_ver, corrupt, gen=None, lib=None):
        for d in ("src/runtime", "test/common", "tools"):
            os.makedirs(os.path.join(tmp, d), exist_ok=True)
        for f in (
            MINI_REL,
            "src/runtime/webgpu.cpp",
            "test/common/gpu_context.h",
            "vcpkg.json",
        ):
            shutil.copy(os.path.join(REPO, f), os.path.join(tmp, f))
        shutil.copy(
            os.path.abspath(__file__), os.path.join(tmp, "tools", "mini-webgpu-tool.py")
        )
        if gen:
            gen_dir = os.path.join(
                tmp, "build/vcpkg_installed/arm64-osx-halide/include/dawn"
            )
            os.makedirs(gen_dir)
            shutil.copy(gen, os.path.join(gen_dir, "webgpu.h"))

            lib_dir = os.path.join(tmp, "build/vcpkg_installed/arm64-osx-halide/lib")
            os.makedirs(lib_dir)
            shutil.copy(lib, os.path.join(lib_dir, os.path.basename(lib)))

        mini = os.path.join(tmp, MINI_REL)
        with open(mini) as fh:
            text = fh.read()
        old = f'{MARKER} "20260624.223603"'
        assert old in text, f"sample marker not found in {MINI_REL}"
        if marker_ver:
            text = text.replace(old, f'{MARKER} "{marker_ver}"')
        else:
            text = text.replace(old + "\n", "")
        if corrupt:  # simulate a field Dawn inserted mid-struct
            text = text.replace(corrupt, corrupt + "\n    size_t insertedField;", 1)
        with open(mini, "w") as fh:
            fh.write(text)

    for name, ver, corrupt, want in CASES:
        if corrupt and not gen:
            # check() legitimately has nothing to compare struct layouts against
            # without a Dawn-generated header, so it cannot detect the drift this
            # fixture injects. Record it as skipped, not as an expected failure.
            results.append(
                (name, "skip", "skipped", True, ["no Dawn-generated header available"])
            )
            continue
        tmp = tempfile.mkdtemp(prefix="miniwebgpu_")
        try:
            fixture(tmp, ver, corrupt, gen, lib)
            got, out = run(tmp, ["check", "build"])
            key = [
                line
                for line in out.splitlines()
                if line.startswith(("FAIL", "PASS", "struct layout"))
            ]
            results.append((name, want, got, got == want, key[:2]))
        finally:
            shutil.rmtree(tmp)

    # `check` before Configure: marker rule must still pass with no build tree.
    tmp = tempfile.mkdtemp(prefix="miniwebgpu_notree_")
    try:
        fixture(tmp, "20260624.223603", None)
        got, out = run(tmp, ["check", "build"])
        key = [line for line in out.splitlines() if line.startswith("NOTE")]
        results.append(("no build tree", 0, got, got == 0, key[:1]))
    finally:
        shutil.rmtree(tmp)

    # pinstatus is advisory: neither an available update nor a missing vcpkg may
    # fail CI; only --strict turns the signal into a nonzero exit. Fake vcpkg roots
    # make the verdict independent of what this machine has installed.
    # (name, version for the fake ports/dawn/vcpkg.json, strict, expected exit)
    for name, ver, strict, want in (
        ("pin newer", "29990101.1", False, 0),
        ("pin newer strict", "29990101.1", True, 1),
        ("pin older", "20200101.1", True, 0),
        ("pin unreadable", None, True, 0),
    ):
        tmp = tempfile.mkdtemp(prefix="miniwebgpu_pin_")
        try:
            fixture(tmp, "20260624.223603", None)
            root = os.path.join(tmp, "vcpkgroot")
            os.makedirs(os.path.join(root, "ports", "dawn"))
            if ver:
                with open(os.path.join(root, "ports", "dawn", "vcpkg.json"), "w") as fh:
                    json.dump({"name": "dawn", "version": ver, "port-version": 1}, fh)
            argv = ["pinstatus", "--vcpkg-root", root]
            if strict:
                argv.append("--strict")
            got, out = run(tmp, argv)
            verdict = [line for line in out.splitlines() if line.startswith("PIN-")]
            results.append((name, want, got, got == want, verdict[:1]))
        finally:
            shutil.rmtree(tmp)

    # sync must be idempotent: rendering twice yields identical text.
    if gen:
        try:
            pin = dawn_pin()
            with open(MINI) as fh:
                first = render(gen, fh.read(), pin)
            second = render(gen, first, pin)
            ok = first == second
            results.append(
                ("sync idempotent", True, ok, ok, ["render(render(x)) == render(x)"])
            )
        except SystemExit as e:
            results.append(("sync idempotent", True, str(e), False, []))
    else:
        results.append(
            (
                "sync idempotent",
                "skip",
                "skipped",
                True,
                ["no Dawn-generated header available"],
            )
        )

    rc = 0
    for name, want, got, ok, detail in results:
        if not ok:
            rc = 1
        status = "OK" if ok else "*** MISMATCH ***"
        print(f"{name:<14} expect {want}, got {got}  {status}  {detail}")
    print("PASS" if rc == 0 else "FAIL")
    return rc


def cmd_selftest(args):
    gen = resolve_one(args.dawn_header, required=False)
    lib = resolve_one(args.lib, required=False) if gen else None
    if not gen:
        print(
            "NOTE: no Dawn-generated webgpu.h found; running the reduced selftest "
            "(marker rules only). Pass --dawn-header <path> to exercise the "
            "struct/enum/symbol paths."
        )
    return selftest(gen, lib)


# ---------------------------------------------------------------------------- main


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser(
        "check",
        help="verify mini_webgpu.h matches the Dawn pin "
        "(and the build tree, if present)",
    )
    c.add_argument(
        "build_dir",
        nargs="?",
        default="build",
        help="CMake build dir to find vcpkg_installed in (default: build)",
    )
    c.set_defaults(func=cmd_check)

    s = sub.add_parser(
        "sync", help="rewrite mini_webgpu.h from a Dawn-generated header"
    )
    s.add_argument(
        "dawn_header",
        nargs="?",
        default=None,
        help="path or glob for the Dawn-generated webgpu.h, e.g. "
        "build/vcpkg_installed/*/include/dawn/webgpu.h "
        "(default: search common build-dir layouts)",
    )
    s.add_argument(
        "--build-dir",
        default=None,
        help="build dir to verify against (default: derive from the header path)",
    )
    s.set_defaults(func=cmd_sync)

    q = sub.add_parser(
        "pinstatus",
        help="report (advisory only) whether a newer Dawn is available than the pin",
    )
    q.add_argument(
        "--vcpkg-root",
        default=None,
        help="vcpkg instance to ask; default is $VCPKG_INSTALLATION_ROOT, then "
        "$VCPKG_ROOT, then `vcpkg` on PATH. On GitHub runners "
        "$VCPKG_INSTALLATION_ROOT is the image's own snapshot, which is what "
        "constrains the pin there.",
    )
    q.add_argument(
        "--remote",
        action="store_true",
        help="also query vcpkg master on GitHub, to show what a newer baseline would offer",
    )
    q.add_argument(
        "--strict",
        action="store_true",
        help="exit 1 when an update is available (off by default so CI stays green)",
    )
    q.set_defaults(func=cmd_pinstatus)

    t = sub.add_parser(
        "selftest",
        help="exercise check's pass/drift/broken paths against temp fixtures",
    )
    t.add_argument(
        "--dawn-header",
        default=GEN_GLOBS,
        help="Dawn-generated header (path or glob) for the fixture's upstream",
    )
    t.add_argument(
        "--lib",
        default=LIB_GLOBS,
        help="built libwebgpu_dawn (path or glob) for the export-table fixture",
    )
    t.set_defaults(func=cmd_selftest)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
