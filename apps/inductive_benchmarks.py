#!/usr/bin/env python3
"""Builds and runs every inductive-Func benchmark app in one or two
configurations and prints one table: per app and config, the inductive
time, the RDom (materializing) time, and the fastest baseline.

    apps/inductive_benchmarks.py [--only app,app] [--out results.md]

Raw outputs are kept under apps/inductive_benchmarks_logs/. Apps whose
external baseline is unavailable on this box (Boost.odeint, oneTBB,
Triton, scipy, Julia) report the best available baseline and say so.
Every app is rebuilt from clean for each config, since their Makefiles
do not track knob changes.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

APPS = Path(__file__).resolve().parent
LOGS = APPS / "inductive_benchmarks_logs"
VENV = Path.home() / ".claude/jobs/6e7f4185/tmp/venv/bin/python"
JULIA = Path.home() / ".juliaup/bin/julia"
NCORES = os.cpu_count() // 2 or 1  # physical cores: parallel configs use one thread per core


# Every process the driver runs, Halide runners and baselines alike, gets
# jemalloc configured to keep freed memory: blocks above 8 MB in a normal
# arena rather than the immediately purged oversize one, and no decay. A
# pipeline's scratch is freed at the end of every run, and past a few MB
# the default allocators hand it back to the kernel, so each timed run would
# otherwise pay a first-touch page fault per 4 KB of it; retaining is what
# any application that reuses its work buffers gets. Without the library the
# numbers change materially, so its absence is an error, not a fallback.
JEMALLOC = "/lib/x86_64-linux-gnu/libjemalloc.so.2"
# thp:always backs every allocation with transparent huge pages where the
# kernel allows (this box has them in madvise mode): the serial forms that
# stream gigabyte signals otherwise vary by 30% run to run with where the
# 4 KB pages land, and huge pages take that spread away.
MALLOC_CONF = "oversize_threshold:0,dirty_decay_ms:-1,muzzy_decay_ms:-1,thp:always"
# Single-threaded rows run pinned to one core, so that which chiplet the
# thread lands on, and any migration during the run, is not part of the
# measurement. The machine is one NUMA node, so there is no memory to place.
PIN_CORE = 4


def sh(cmd, cwd, env=None, log=None, check=True, pin=False):
    e = dict(os.environ)
    # Julia brings its own allocator regime and crashes at exit under the
    # preload (free(): invalid size, after printing its result), so it runs
    # without it; its 32 MB working array is retained by its own runtime.
    if str(JULIA) not in cmd:
        e["LD_PRELOAD"] = JEMALLOC
        e["MALLOC_CONF"] = MALLOC_CONF
    e.update(env or {})
    if pin:
        cmd = f"taskset -c {PIN_CORE} {cmd}"
    p = subprocess.run(cmd, shell=True, cwd=str(cwd), env=e,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if log:
        log.write_text(p.stdout)
    if check and p.returncode != 0:
        raise RuntimeError(f"{cmd!r} in {cwd} failed:\n{p.stdout[-2000:]}")
    return p.stdout


def us_row(text, label):
    """Our runners: '  <label> <number> us'. Returns milliseconds."""
    m = re.search(r"^\s+" + re.escape(label) + r"\s+([\d.]+)\s+us", text, re.M)
    return float(m.group(1)) / 1e3 if m else None


def hb_rows(text):
    """bench_harness rows: '  <variant>  <min ms>  <median ms> ...'. Returns {variant: min ms}."""
    rows = {}
    for m in re.finditer(r"^  (\S.*?)\s{2,}([\d.]+)\s+([\d.]+)\s", text, re.M):
        rows[m.group(1).strip()] = float(m.group(2))
    return rows


def best(cands):
    """(name, ms) of the fastest available among [(name, ms-or-None)]."""
    c = [(n, t) for n, t in cands if t is not None]
    return min(c, key=lambda x: x[1]) if c else (None, None)


# Each config returns dict(params, ind, rdom, base_name, base).
def cpu_biquads(par):
    d = APPS / "cpu_biquads"
    knobs = dict(SECTIONS=8, CHANNELS=256 if par else 32, SAMPLES=1 << 20 if par else 8 << 20, PAR=str(par).lower())
    kv = " ".join(f"{k}={v}" for k, v in knobs.items())
    sh("make -s clean", d)
    out = sh(f"make -s {kv} test", d, log=LOGS / f"cpu_biquads_par{par}.txt", pin=not par)
    scipy = None
    if VENV.exists() and not par:  # scipy is single-threaded; compare it in the serial config
        so = sh(f"{VENV} sosfilt_bench.py {knobs['CHANNELS']} {knobs['SAMPLES']} {knobs['SECTIONS']}", d, pin=True)
        scipy = us_row(so, "scipy.sosfilt")
    # Intel IPP's multi-channel IIR (ippsIIR_32f_P), threaded across channels
    # in the parallel config, when the runner was built with it.
    ipp = us_row(out, "ipp")
    # The "Finding Fast Filters" template library's strided IIR cascade
    # (Ma et al.), vectorized across a block of channels like our schedule.
    fff = us_row(out, "fff")
    bn, bt = best([("scipy.sosfilt", scipy), ("Intel IPP ippsIIR_32f_P" + (" (threaded)" if par else ""), ipp),
                   ("Finding Fast Filters strided cascade" + (" (threaded)" if par else ""), fff)])
    return dict(params=f"{knobs['SECTIONS']} sections, {knobs['CHANNELS']} ch x {knobs['SAMPLES']} samples, "
                       f"{'all cores' if par else '1 thread'}",
                ind=us_row(out, "inductive"), rdom=us_row(out, "rdom"), base_name=bn, base=bt)


def cpu_rng(par):
    d = APPS / "cpu_rng"
    knobs = dict(LANES=1024 if par else 32, STEPS=131072 if par else 1 << 22, PAR=str(par).lower())
    kv = " ".join(f"{k}={v}" for k, v in knobs.items())
    sh("make -s clean", d)
    out = sh(f"make -s {kv} test", d, log=LOGS / f"cpu_rng_par{par}.txt", pin=not par)
    julia = None
    if JULIA.exists() and not par:  # Julia's rand! is single-threaded
        jo = sh(f"{JULIA} julia_bench.jl {2 * knobs['LANES']} {knobs['STEPS']}", d, pin=True)
        julia = us_row(jo, "julia xoshiro")
    bn, bt = best([("hand AVX-512 kernel", us_row(out, "simd C++")), ("Julia rand!", julia)])
    return dict(params=f"{knobs['LANES']} streams x {knobs['STEPS']} steps, {'all cores' if par else '1 thread'}",
                ind=us_row(out, "inductive"), rdom=us_row(out, "rdom"), base_name=bn, base=bt)


def cpu_alignment(par):
    d = APPS / "cpu_alignment"
    knobs = dict(QLEN=1024, TLEN=1024, BATCH=4096 if par else 128, PAR=str(par).lower())
    kv = " ".join(f"{k}={v}" for k, v in knobs.items())
    sh("make -s clean", d)
    out = sh(f"make -s {kv} test", d, log=LOGS / f"cpu_alignment_par{par}.txt", pin=not par)
    comp = us_row(out, "compaction") or 0.0
    bn, bt = best([("ksw2 (minimap2 kernel)", us_row(out, "ksw2 sse")), ("parasail", us_row(out, "parasail"))])
    return dict(params=f"1024x1024 x {knobs['BATCH']} pairs, {'all cores' if par else '1 thread'}, fill+traceback+cigar",
                ind=us_row(out, "int8 ind + traceback"),
                rdom=(us_row(out, "int8 rdom") or 0) + comp, base_name=bn, base=bt)


def kalman_ll(threads):
    d = APPS / "kalman_ll"
    sh("make -s clean && make -s bin/ar_ll", d)
    # A task is two vectors of series, 32 in float, so the threaded row takes
    # enough series to give every core work.
    B, T = (1024 if threads > 1 else 256), 16384
    out = sh(f"bin/ar_ll {B} {T}", d, env={"HL_NUM_THREADS": str(threads)}, pin=threads == 1,
             log=LOGS / f"kalman_ll_t{threads}.txt")
    r = hb_rows(out)
    return dict(params=f"{B} series x {T} steps, {threads} thread{'s' if threads > 1 else ''}",
                ind=r.get("inductive (fold t -> 2)"), rdom=r.get("non-inductive (materialize)"),
                base_name=None, base=None)


def suite_bin(name):
    d = APPS / "inductive_suite"
    sh("make -s all", d)
    return d / "bin" / name


def viterbi(S, M, T):
    out = sh(f"{suite_bin('viterbi_log')} {S} {M} {T}", APPS / "inductive_suite",
             env={"HL_NUM_THREADS": "1"}, log=LOGS / f"viterbi_{S}_{M}_{T}.txt", pin=True)
    r = hb_rows(out)
    return dict(params=f"{S} states, {M} symbols, T={T}, 1 thread",
                ind=r.get("inductive FOLDED (fold t -> 2)"), rdom=r.get("non-inductive (materialize)"),
                base_name=None, base=None)


def chebyshev(n, M):
    out = sh(f"{suite_bin('chebyshev_test')} {n} {M}", APPS / "inductive_suite",
             env={"HL_NUM_THREADS": "1"}, log=LOGS / f"chebyshev_{n}_{M}.txt", pin=True)
    r = hb_rows(out)
    return dict(params=f"n={n} dense SPD, {M} iterations, 1 thread (in-cache control)",
                ind=r.get("inductive FOLDED (fold -> 3 cols)"),
                rdom=r.get("non-inductive FULL materialize (M+1 cols)"),
                base_name="hand-written mod-3 ring", base=r.get("non-inductive mod-3 ring (3 cols)"))


def ode(D, B, T):
    b = suite_bin("ode_observer_sparse_fused_test")
    if not b.exists():
        return dict(params=f"D={D}, B={B}, T={T}", ind=None, rdom=None,
                    base_name="Boost.odeint (needs libboost-dev)", base=None, skipped=True)
    out = sh(f"{b} {D} {B} {T}", APPS / "inductive_suite", env={"HL_NUM_THREADS": "1"}, pin=True,
             log=LOGS / f"ode_{D}_{B}_{T}.txt")
    r = hb_rows(out)
    return dict(params=f"Allen-Cahn D={D}, batch {B}, T={T}, 1 thread",
                ind=r.get("inductive FOLDED (fold n -> 2)"), rdom=r.get("non-inductive (materialize)"),
                base_name="Boost.odeint", base=r.get("Boost.odeint (rk4 init + observer)"))


def prefixsum(W, H, threads):
    env = {"HL_NUM_THREADS": str(threads)}
    d = APPS / "inductive_suite"
    pin = threads == 1
    ind = hb_rows(sh(f"{suite_bin('prefixsum_bench')} {W} {H}", d, env=env, pin=pin,
                     log=LOGS / f"prefixsum_ind_{W}_{H}_t{threads}.txt"))
    rd = hb_rows(sh(f"{suite_bin('prefixsum_bench_rdom')} {W} {H}", d, env=env, pin=pin,
                    log=LOGS / f"prefixsum_rdom_{W}_{H}_t{threads}.txt"))
    tbb = d / "bin/prefixsum_bench_tbb"
    tb = None
    if tbb.exists():
        tb = hb_rows(sh(f"{tbb}", d, env=env, pin=pin)).get("oneTBB parallel_scan (fused)")
    return dict(params=f"{W} x {H} rows, running-mean consumer, {threads} thread{'s' if threads > 1 else ''}",
                ind=ind.get("inductive FOLDED (fold x -> 1 accum)"),
                rdom=rd.get("non-inductive (RDom, materialize row)"),
                base_name="oneTBB parallel_scan" if tb is not None else "oneTBB (needs libtbb-dev)", base=tb)


def mamba2(direction):
    # Each side at its own best chunk: Triton's kernels prefer 256; the
    # Halide forward matches it there, the Halide backward is best at 128
    # (its causal pruning makes smaller chunks cheaper).
    d = APPS / "cuda_mamba2"
    shape = dict(SEQ=4096, STATE=128, CHANNELS=64, HEADS=128, GROUPS=1)
    ours_chunk = 128 if direction == "bwd" else 256
    triton_chunk = 256
    kv = " ".join(f"{k}={v}" for k, v in shape.items()) + f" CHUNK={ours_chunk} WMMA=true"
    target = "test_bwd" if direction == "bwd" else "test"
    label = "Halide mamba2 bwd" if direction == "bwd" else "Halide mamba2"
    times = {}
    for scan in ("inductive", "rdom"):
        sh("make -s clean", d)
        out = sh(f"make -s {kv} SCAN={scan} {target}", d, log=LOGS / f"mamba2_{direction}_{scan}.txt")
        m = re.search(re.escape(label) + r"\s+[\d.]+ GFlop/s\s+([\d.]+) us", out)
        times[scan] = float(m.group(1)) / 1e3 if m else None
    triton = None
    if VENV.exists():
        try:
            to = sh(f"{VENV} triton_bench.py {shape['SEQ']} {shape['STATE']} {shape['CHANNELS']} "
                    f"{shape['HEADS']} {shape['GROUPS']} {triton_chunk}", d,
                    log=LOGS / f"mamba2_triton_{direction}.txt")
            triton = us_row(to, f"triton {direction}")
        except RuntimeError:
            pass
    return dict(params=f"{direction}, seq {shape['SEQ']}, state {shape['STATE']}, {shape['HEADS']} heads x "
                       f"{shape['CHANNELS']}, chunk {ours_chunk} (Triton {triton_chunk}), RTX 5060 Ti",
                ind=times["inductive"], rdom=times["rdom"],
                base_name="Triton (mamba_ssm)" if triton else "Triton (unavailable)", base=triton)


def flash_attention():
    # The flash filter (an inductive online softmax over key chunks) against
    # its RDom-only form: the same online softmax with the running maximum
    # and row sum carried at the accumulator's shape, as one Tuple update
    # over the key chunks, at its own best chunk. The runner also drives a
    # non-flash fused filter that does not
    # launch at this shape (a known, pre-existing failure), so run the
    # binary directly and take the rows. Reference: PyTorch's
    # FlashAttention-2 SDPA backend at the same shape.
    d = APPS / "cuda_attention"
    shape = dict(QUERIES=65536, KEYS=1024, DEPTH=64, OUT_DEPTH=64)
    kv = " ".join(f"{k}={v}" for k, v in shape.items())
    sh("make -s clean", d)
    sh(f"make -s {kv} bin/host-cuda/runner", d)
    out = sh("bin/host-cuda/runner", d, log=LOGS / "flash_attention.txt", check=False)
    m = re.search(r"Halide flash attention\s+[\d.]+ GFlop/s\s+([\d.]+) us", out)
    ind = float(m.group(1)) / 1e3 if m else None
    m = re.search(r"Halide flash attention \(rdom\)\s+[\d.]+ GFlop/s\s+([\d.]+) us", out)
    rdom = float(m.group(1)) / 1e3 if m else None
    torch_flash = None
    if VENV.exists():
        try:
            to = sh(f"{VENV} torch_bench.py {shape['QUERIES']} {shape['KEYS']} {shape['DEPTH']}", d,
                    log=LOGS / "flash_attention_torch.txt")
            torch_flash = us_row(to, "torch flash")
        except RuntimeError:
            pass
    return dict(params=f"{shape['QUERIES']} queries x {shape['KEYS']} keys, depth {shape['DEPTH']}, fp16, "
                       f"chunk 64, RTX 5060 Ti",
                ind=ind, rdom=rdom,
                base_name="FlashAttention-2 (torch SDPA)" if torch_flash else "torch flash (unavailable)",
                base=torch_flash)


SUITE = [
    ("cpu_biquads", lambda: cpu_biquads(False)),
    ("cpu_biquads", lambda: cpu_biquads(True)),
    ("cpu_rng", lambda: cpu_rng(False)),
    ("cpu_rng", lambda: cpu_rng(True)),
    ("cpu_alignment", lambda: cpu_alignment(False)),
    ("cpu_alignment", lambda: cpu_alignment(True)),
    ("kalman_ll", lambda: kalman_ll(1)),
    ("kalman_ll", lambda: kalman_ll(NCORES)),
    ("viterbi", lambda: viterbi(16, 4, 320000)),
    ("viterbi", lambda: viterbi(64, 8, 50000)),
    ("ode", lambda: ode(1024, 1, 32768)),
    ("prefixsum", lambda: prefixsum(1 << 20, 32, 1)),
    ("prefixsum", lambda: prefixsum(1 << 20, 32, NCORES)),
    ("chebyshev", lambda: chebyshev(2048, 100)),
    ("cuda_mamba2", lambda: mamba2("fwd")),
    ("cuda_mamba2", lambda: mamba2("bwd")),
    ("flash_attention", flash_attention),
]


def fmt(ms):
    return "-" if ms is None else (f"{ms:.3f}" if ms < 1 else f"{ms:.1f}")


def ratio(a, b):
    return "-" if a is None or b is None or a == 0 else f"{b / a:.2f}x"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", help="comma-separated app names; other apps' rows come from the cache")
    ap.add_argument("--out", default=str(APPS / "inductive_benchmarks_results.md"))
    ap.add_argument("--render", action="store_true", help="only render the table from the cache")
    args = ap.parse_args()
    LOGS.mkdir(exist_ok=True)
    only = set(args.only.split(",")) if args.only else None
    if not args.render and not Path(JEMALLOC).exists():
        sys.exit(f"{JEMALLOC} is needed to retain freed memory across runs (apt install libjemalloc2)")
    # Rows are cached per (app, config index) so a partial run updates the
    # table instead of replacing it.
    import json
    cache_path = LOGS / "results.json"
    cache = json.loads(cache_path.read_text()) if cache_path.exists() else {}
    for idx, (name, fn) in enumerate(SUITE):
        key = f"{idx}:{name}"
        if args.render or (only and name not in only):
            continue
        print(f"== {name} ==", flush=True)
        try:
            r = fn()
        except Exception as e:
            print(f"   FAILED: {str(e)[:400]}", flush=True)
            r = dict(params="(failed)", ind=None, rdom=None, base_name=None, base=None)
        r["app"] = name
        cache[key] = r
        cache_path.write_text(json.dumps(cache, indent=1))
        print(f"   {r['params']}: inductive {fmt(r['ind'])} ms, rdom {fmt(r['rdom'])} ms, "
              f"baseline {r['base_name'] or '-'} {fmt(r['base'])} ms", flush=True)
    rows = [cache[f"{idx}:{name}"] for idx, (name, _) in enumerate(SUITE) if f"{idx}:{name}" in cache]

    hdr = "| App | Config | Inductive (ms) | RDom (ms) | Fastest baseline (ms) | Baseline | RDom / ind | Baseline / ind |"
    sep = "|---|---|---:|---:|---:|---|---:|---:|"
    lines = [hdr, sep]
    for r in rows:
        lines.append(f"| {r['app']} | {r['params']} | {fmt(r['ind'])} | {fmt(r['rdom'])} | {fmt(r['base'])} | "
                     f"{r['base_name'] or '-'} | {ratio(r['ind'], r['rdom'])} | {ratio(r['ind'], r['base'])} |")
    table = "\n".join(lines)
    print("\n" + table)
    notes = (
        "Times are milliseconds per run: inductive = the folded inductive-Func form, RDom = the same "
        "algorithm as update definitions (materializing), baseline = the fastest non-Halide implementation "
        "available on this machine. Measured on a Threadripper 9970X (Zen 5, 32 cores) and an RTX 5060 Ti.\n\n"
        "Every Halide form is verified against its app's reference before timing (the alignment rows "
        "byte-exact against ksw2, the rng rows bit-exact against Julia's rand! seeding, the GPU rows against "
        "serial references). Baselines are threaded across independent problems in the all-cores rows where "
        "the library allows it (ksw2, parasail, oneTBB); the rng hand kernel and Julia's rand! are "
        "single-threaded, so the rng all-cores baseline is the single-thread hand kernel. The mamba2 rows "
        "compare each side at its own best chunk (Triton prefers 256; the Halide backward is best at 128), "
        "with the tensor-core schedules (WMMA=true). Flash attention's RDom form is the same online softmax "
        "with the running maximum and row sum carried at the accumulator's shape, broadcast across its "
        "columns, so that one Tuple update over the key chunks advances all three (Halide fuses no "
        "dependent stages, and a per-row Func may not read the state its update feeds); the rescalings are "
        "then paid per element rather than per row, the same tile verbs and staging otherwise. For scale, "
        "cuBLAS + softmax + cuBLAS is about 13x slower than the flash filter. Chebyshev is the "
        "intended in-cache control, where folding buys nothing. Outputs that are written once and never "
        "read back are streamed in every form (rng, biquads, the alignment direction plane); the JIT apps "
        "(kalman, viterbi, ode) and the alignment runner free their scratch at the end of every run, and "
        "past a few MB the default allocators hand it back to the kernel, so every process the driver runs, "
        "baselines included, uses jemalloc configured to keep freed memory (oversize_threshold:0, no "
        "decay), which is what an application reusing its work buffers gets. It matters where the scratch "
        "is large: without retention the ode RDom row is 23 ms rather than 7, and every alignment form pays "
        "about 230 ms of first-touch faults on its 64 MB per-task direction planes; the baselines are within "
        "noise either way, scipy a few percent faster. The same allocator backs everything with transparent "
        "huge pages, and single-threaded rows run pinned to one core: the serial forms that stream gigabyte "
        "signals otherwise vary by 30% from run to run with where their pages land. The Python baselines "
        "report the best sample, as Halide's harness does. Every measurement, Halide forms and baselines in "
        "every language, follows one protocol (apps/support/bench_harness.h): three untimed runs, thirty "
        "timed trials, the best reported with the median kept for the spread; on the GPU a trial is ten "
        "launches and one device synchronization, divided by ten, so the time includes completion but not a "
        "synchronization per launch.\n\n")
    Path(args.out).write_text(notes + table + "\n")
    print(f"\nwritten to {args.out}")


if __name__ == "__main__":
    main()
