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
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

APPS = Path(__file__).resolve().parent
LOGS = APPS / "inductive_benchmarks_logs"
SCRATCH = Path(os.environ.get("HB_SCRATCH", "/tmp")) / "hb_peak"
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
# The alignment rows compare at parasail's instruction set.
ALIGN_TARGET = "x86-64-linux-sse41-avx-avx2"
# All-cores rows run one thread per physical core, on the first hardware
# thread of every core: the forms that stream their state through memory
# are bound by each chiplet's link to the memory controller, so where the
# scheduler lands their threads otherwise moves them by up to 1.7x. Halide
# forms and baselines get the same mask and the same thread count.
CORES_MASK = f"0-{NCORES - 1}"


def sh(cmd, cwd, env=None, log=None, check=True, pin=False, cores=False):
    """Run cmd in cwd under the benchmarking environment. pin: one core,
    for single-threaded rows; cores: one thread per physical core."""
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
    elif cores:
        e.setdefault("HL_NUM_THREADS", str(NCORES))
        cmd = f"taskset -c {CORES_MASK} {cmd}"
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


def hb_bytes(text):
    """bench_harness rows carry a byte-exact 'state_bytes=N'. Returns
    {variant: peak_heap_bytes} measured by the Halide profiler (memory_peak)."""
    out = {}
    for m in re.finditer(r"^  (\S.*?)\s{2,}.*state_bytes=([\d.eE+]+)", text, re.M):
        out[m.group(1).strip()] = float(m.group(2))
    return out


def mb(b):
    """Bytes to MB, or None."""
    return None if b is None else b / 1e6


def peak_from_json(path, names):
    """The Halide profiler's JSON report lists each pipeline with its
    memory_peak (peak halide_malloc bytes), before its Funcs'. Returns
    {pipeline_name: peak_bytes} for the named pipelines."""
    try:
        doc = json.loads(Path(path).read_text())
    except Exception:
        return {}
    out = {}

    def walk(o):
        if isinstance(o, dict):
            n, mp = o.get("name"), o.get("memory_peak")
            if n in names and mp is not None and n not in out:
                out[n] = float(mp)
            for v in o.values():
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)

    walk(doc)
    return out


def aot_peak_mb(d, kv, target_kv, ind_name, rdom_name, tag, pin=True, cores=False):
    """Peak heap of the inductive and RDom pipelines of an AOT app, from the
    Halide profiler. Builds a profiled copy (separate bin dir, does not touch
    the timing build) and runs it once with the JSON report on."""
    js = SCRATCH / f"peak_{tag}.json"
    try:
        js.unlink()
    except FileNotFoundError:
        pass
    env = {"HL_PROFILER_JSON_OUTPUT": str(js), "HB_TRIALS": "1", "HB_WARMUP": "0"}
    try:
        # The profiled forms build into the timing build's directory for apps
        # whose profile knob does not change the target, so clean first; the
        # timing pass has already run by now.
        sh("make -s clean", d)
        sh(f"make -s {kv} {target_kv} test", d, env=env, pin=pin, cores=cores,
           log=LOGS / f"peak_{tag}.txt")
    except Exception as e:
        print(f"   peak measurement for {tag} failed: {str(e)[:200]}", flush=True)
        return None, None
    p = peak_from_json(js, {ind_name, rdom_name})
    return mb(p.get(ind_name)), mb(p.get(rdom_name))


def gpu_peak_mb(d, build_cmd, names, tag):
    """Peak heap the Halide profiler reports for a GPU build's pipeline(s).
    A separate profiled build+run; note the profiler's memory_peak is the
    host-side heap, which for these tensor-core pipelines is the staging and
    working buffers, not the raw device tensors."""
    js = SCRATCH / f"peak_{tag}.json"
    try:
        js.unlink()
    except FileNotFoundError:
        pass
    env = {"HL_PROFILER_JSON_OUTPUT": str(js), "HB_TRIALS": "1", "HB_WARMUP": "0"}
    try:
        sh("make -s clean", d)
        sh(build_cmd, d, env=env, log=LOGS / f"peak_{tag}.txt")
    except Exception as e:
        print(f"   peak measurement for {tag} failed: {str(e)[:200]}", flush=True)
        return {}
    return {n: mb(b) for n, b in peak_from_json(js, set(names)).items()}


def best(cands):
    """(name, ms) of the fastest available among [(name, ms-or-None)]."""
    c = [(n, t) for n, t in cands if t is not None]
    return min(c, key=lambda x: x[1]) if c else (None, None)


# Every non-Halide implementation tried per app, fastest-baseline candidates
# included, so the table records what has already been measured and found
# slower (see the app READMEs for the numbers behind the ones the driver
# does not run).
TRIED = {
    "cpu_biquads": ["Finding Fast Filters strided cascade", "Intel IPP ippsIIR_32f_P", "scipy.sosfilt",
                    "Julia DSP.jl filt", "FFmpeg biquad", "torchaudio lfilter"],
    "cpu_rng": ["Julia rand!", "numpy PCG64 (a different generator)", "scalar C++ loop"],
    "cpu_alignment": ["parasail", "ksw2 (minimap2 kernel)", "ksw2 scalar"],
    "viterbi": [],
    "ode": ["Boost.odeint", "fused C++ loop"],
    "prefixsum": ["oneTBB parallel_for rows", "oneTBB parallel_scan"],
    "chebyshev": ["hand-written mod-3 ring"],
    "cuda_mamba2": ["Triton (mamba_ssm)"],
    "flash_attention": ["FlashAttention-2 (torch SDPA)", "cuDNN SDPA (torch)", "torch memory-efficient SDPA",
                        "cuBLAS + softmax + cuBLAS"],
}


def slower_tried(app, base_name):
    base = (base_name or "").replace(" (threaded)", "")
    return [n for n in TRIED.get(app, []) if n != base]


# Each config returns dict(params, ind, rdom, base_name, base).
def cpu_biquads(par):
    d = APPS / "cpu_biquads"
    # The all-cores signal has 32 blocks of 32 channels: one task per core.
    knobs = dict(SECTIONS=8, CHANNELS=1024 if par else 32, SAMPLES=1 << 18 if par else 8 << 20, PAR=str(par).lower())
    kv = " ".join(f"{k}={v}" for k, v in knobs.items())
    sh("make -s clean", d)
    out = sh(f"make -s {kv} test", d, log=LOGS / f"cpu_biquads_par{par}.txt", pin=not par, cores=par)
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
    ind_mb, rdom_mb = aot_peak_mb(d, kv, "HL_TARGET=host-profile", "biquads_ind", "biquads_rdom",
                                 f"biquads_par{par}", pin=not par, cores=par)
    return dict(ind_mb=ind_mb, rdom_mb=rdom_mb,
                params=f"{knobs['SECTIONS']} sections, {knobs['CHANNELS']} ch x {knobs['SAMPLES']} samples, "
                       f"{'all cores' if par else '1 thread'}",
                ind=us_row(out, "inductive"), rdom=us_row(out, "rdom"), base_name=bn, base=bt)


def cpu_rng(par):
    d = APPS / "cpu_rng"
    knobs = dict(LANES=1024 if par else 32, STEPS=131072 if par else 1 << 22, PAR=str(par).lower())
    kv = " ".join(f"{k}={v}" for k, v in knobs.items())
    sh("make -s clean", d)
    out = sh(f"make -s {kv} test", d, log=LOGS / f"cpu_rng_par{par}.txt", pin=not par, cores=par)
    julia = None
    if JULIA.exists():
        threads = f"-t {NCORES}" if par else ""
        jo = sh(f"{JULIA} {threads} julia_bench.jl {2 * knobs['LANES']} {knobs['STEPS']}", d, pin=not par, cores=par,
                log=LOGS / f"cpu_rng_julia_par{par}.txt")
        julia = us_row(jo, "julia xoshiro")
    # The runner's AVX-512 port of Julia's kernel is a control on the Halide
    # forms, not a baseline; Julia's own rand! is the third-party number.
    bn, bt = best([("Julia rand!" + (" (threaded)" if par else ""), julia),
                   ("scalar C++ loop", us_row(out, "scalar C++"))])
    ind_mb, rdom_mb = aot_peak_mb(d, kv, "HL_TARGET=host-profile", "rng_ind", "rng_rdom",
                                 f"rng_par{par}", pin=not par, cores=par)
    return dict(ind_mb=ind_mb, rdom_mb=rdom_mb,
                params=f"{knobs['LANES']} streams x {knobs['STEPS']} steps, {'all cores' if par else '1 thread'}",
                ind=us_row(out, "inductive"), rdom=us_row(out, "rdom"), base_name=bn, base=bt)


def cpu_alignment(par):
    d = APPS / "cpu_alignment"
    knobs = dict(QLEN=1024, TLEN=1024, BATCH=4096 if par else 128, PAR=str(par).lower())
    kv = " ".join(f"{k}={v}" for k, v in knobs.items())
    # The Halide forms are compiled for parasail's instruction set (its
    # widest kernels are AVX2), so the table's row is a matched comparison;
    # the AVX-512 numbers are in the app's README.
    sh("make -s clean", d)
    out = sh(f"make -s HL_TARGET={ALIGN_TARGET} {kv} test", d, log=LOGS / f"cpu_alignment_par{par}.txt",
             pin=not par, cores=par)
    comp = us_row(out, "compaction") or 0.0
    bn, bt = best([("ksw2 (minimap2 kernel)", us_row(out, "ksw2 sse")), ("parasail", us_row(out, "parasail")),
                           ("ksw2 scalar", us_row(out, "ksw2 gg"))])
    ind_mb, rdom_mb = aot_peak_mb(d, kv, f"HL_TARGET={ALIGN_TARGET} PROF=-profile", "align8_ind", "align8_rdom",
                                 f"alignment_par{par}", pin=not par, cores=par)
    return dict(ind_mb=ind_mb, rdom_mb=rdom_mb,
                params=f"1024x1024 x {knobs['BATCH']} pairs, {'all cores' if par else '1 thread'}, fill+traceback+cigar, "
                       f"AVX2 (parasail's ISA)",
                ind=us_row(out, "int8 ind + traceback"),
                rdom=(us_row(out, "int8 rdom") or 0) + comp, base_name=bn, base=bt)


def suite_bin(name):
    d = APPS / "inductive_suite"
    sh("make -s all", d)
    return d / "bin" / name


def viterbi(S, M, T, B=1, par=False):
    out = sh(f"{suite_bin('viterbi_log')} {S} {M} {T} {B}", APPS / "inductive_suite",
             env=None if par else {"HL_NUM_THREADS": "1"}, log=LOGS / f"viterbi_{S}_{M}_{T}_{B}.txt",
             pin=not par, cores=par)
    r, bts = hb_rows(out), hb_bytes(out)
    if par:
        params = f"{S} states, {M} symbols, T={T}, {B} decodes, all cores"
    else:
        # One decode on an otherwise idle machine has a chiplet's L3 and the
        # write path to itself. The RDom form's trajectory is S*T*(4+1)
        # bytes; a row whose trajectory fits in the 32 MB L3 is an in-cache
        # control, and one whose trajectory streams is a latency control.
        control = " (in-cache control)" if S * T * 5 <= 32 << 20 else " (latency-bound control)"
        params = f"{S} states, {M} symbols, T={T}, 1 thread{control}"
    return dict(params=params,
                ind=r.get("inductive FOLDED (fold t -> 2)"), rdom=r.get("non-inductive (materialize)"),
                ind_mb=mb(bts.get("inductive FOLDED (fold t -> 2)")),
                rdom_mb=mb(bts.get("non-inductive (materialize)")),
                base_name=None, base=None)


def chebyshev(n, M):
    out = sh(f"{suite_bin('chebyshev_test')} {n} {M}", APPS / "inductive_suite",
             env={"HL_NUM_THREADS": "1"}, log=LOGS / f"chebyshev_{n}_{M}.txt", pin=True)
    r, bts = hb_rows(out), hb_bytes(out)
    return dict(params=f"n={n} dense SPD, {M} iterations, 1 thread (in-cache control)",
                ind=r.get("inductive FOLDED (fold -> 3 cols)"),
                rdom=r.get("non-inductive FULL materialize (M+1 cols)"),
                ind_mb=mb(bts.get("inductive FOLDED (fold -> 3 cols)")),
                rdom_mb=mb(bts.get("non-inductive FULL materialize (M+1 cols)")),
                base_name="hand-written mod-3 ring", base=r.get("non-inductive mod-3 ring (3 cols)"))


def ode(D, B, T, par=False):
    b = suite_bin("ode_observer_sparse_fused_test")
    if not b.exists():
        return dict(params=f"D={D}, B={B}, T={T}", ind=None, rdom=None,
                    base_name="Boost.odeint (needs libboost-dev)", base=None, skipped=True)
    out = sh(f"{b} {D} {B} {T}", APPS / "inductive_suite", env=None if par else {"HL_NUM_THREADS": "1"},
             pin=not par, cores=par, log=LOGS / f"ode_{D}_{B}_{T}.txt")
    r, bts = hb_rows(out), hb_bytes(out)
    threaded = " (threaded)" if par else ""
    bn, bt = best([("Boost.odeint" + threaded, r.get("Boost.odeint (rk4 init + observer)")),
                           ("fused C++ loop" + threaded, r.get("fused C++ loop"))])
    return dict(params=f"Allen-Cahn D={D}, batch {B}, T={T}, {'all cores' if par else '1 thread'}",
                ind=r.get("inductive FOLDED (fold n -> 2)"), rdom=r.get("non-inductive (materialize)"),
                ind_mb=mb(bts.get("inductive FOLDED (fold n -> 2)")),
                rdom_mb=mb(bts.get("non-inductive (materialize)")),
                base_name=bn, base=bt)


def prefixsum(W, H, threads):
    env = {"HL_NUM_THREADS": str(threads)}
    d = APPS / "inductive_suite"
    pin = threads == 1
    ind_out = sh(f"{suite_bin('prefixsum_bench')} {W} {H}", d, env=env, pin=pin,
                 log=LOGS / f"prefixsum_ind_{W}_{H}_t{threads}.txt")
    rdom_out = sh(f"{suite_bin('prefixsum_bench_rdom')} {W} {H}", d, env=env, pin=pin,
                  log=LOGS / f"prefixsum_rdom_{W}_{H}_t{threads}.txt")
    ind, ind_b = hb_rows(ind_out), hb_bytes(ind_out)
    rd, rd_b = hb_rows(rdom_out), hb_bytes(rdom_out)
    tbb = d / "bin/prefixsum_bench_tbb"
    bn, bt = "oneTBB (needs libtbb-dev)", None
    if tbb.exists():
        # Two oneTBB forms: rows in parallel with a serial scan per row, and
        # parallel_scan along each row as well; the faster is the baseline.
        tb = hb_rows(sh(f"{tbb} {W} {H}", d, env=env, pin=pin,
                        log=LOGS / f"prefixsum_tbb_{W}_{H}_t{threads}.txt"))
        bn, bt = best([("oneTBB parallel_for rows", tb.get("oneTBB parallel_for rows (serial scan)")),
                       ("oneTBB parallel_scan", tb.get("oneTBB parallel_scan (fused)"))])
    # The inductive row's label carries its fold and vector widths.
    ind_ms = next((ms for label, ms in ind.items() if label.startswith("inductive FOLDED")), None)
    ind_bytes = next((b for label, b in ind_b.items() if label.startswith("inductive FOLDED")), None)
    return dict(params=f"{W} x {H} rows, running-mean consumer, {threads} thread{'s' if threads > 1 else ''}",
                ind=ind_ms,
                rdom=rd.get("non-inductive (RDom, materialize row)"),
                ind_mb=mb(ind_bytes),
                rdom_mb=mb(rd_b.get("non-inductive (RDom, materialize row)")),
                base_name=bn, base=bt)


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
    times, mem = {}, {}
    pipe = "mamba2_bwd" if direction == "bwd" else "mamba2"
    # The forward's RDom form with the undefined pure definition, its fewest
    # kernels; the backward's RDom form leaves its walks undefined the same way.
    rdom_form = "rdom_undef" if direction == "fwd" else "rdom"
    for scan, key in (("inductive", "inductive"), (rdom_form, "rdom")):
        sh("make -s clean", d)
        out = sh(f"make -s {kv} SCAN={scan} {target}", d, log=LOGS / f"mamba2_{direction}_{scan}.txt")
        m = re.search(re.escape(label) + r"\s+[\d.]+ GFlop/s\s+([\d.]+) us", out)
        times[key] = float(m.group(1)) / 1e3 if m else None
        mp = gpu_peak_mb(d, f"make -s {kv} SCAN={scan} HL_TARGET=host-cuda-profile {target}",
                         [pipe], f"mamba2_{direction}_{key}")
        mem[key] = mp.get(pipe)
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
                ind_mb=mem.get("inductive"), rdom_mb=mem.get("rdom"),
                base_name="Triton (mamba_ssm)" if triton else "Triton (unavailable)", base=triton)


def flash_attention():
    # The flash filter (an inductive online softmax over key chunks) against
    # its RDom-only form: the same online softmax with the running maximum
    # and row sum carried at the accumulator's shape, as one Tuple update
    # over the key chunks, at its own best chunk (the Makefile's RDOM_CHUNK).
    # The runner also drives a non-flash fused filter, which it skips at this
    # shape since it holds every key's scores in registers. All forms compute
    # softmax(QK^T / sqrt(depth)) V and store fp16, as torch's kernels do.
    # Baselines: PyTorch's FlashAttention-2, cuDNN and memory-efficient SDPA
    # backends at the same shape, and an unfused cuBLAS + softmax + cuBLAS.
    d = APPS / "cuda_attention"
    shape = dict(QUERIES=65536, KEYS=1024, DEPTH=64, OUT_DEPTH=64)
    kv = " ".join(f"{k}={v}" for k, v in shape.items())
    sh("make -s clean", d)
    sh(f"make -s {kv} bin/host-cuda/runner", d)
    out = sh("bin/host-cuda/runner", d, log=LOGS / "flash_attention.txt")
    m = re.search(r"^RDOM_CHUNK \?= (\d+)", (d / "Makefile").read_text(), re.M)
    rdom_chunk = m.group(1) if m else "?"
    m = re.search(r"Halide flash attention\s+[\d.]+ GFlop/s\s+([\d.]+) us", out)
    ind = float(m.group(1)) / 1e3 if m else None
    m = re.search(r"Halide flash attention \(rdom\)\s+[\d.]+ GFlop/s\s+([\d.]+) us", out)
    rdom = float(m.group(1)) / 1e3 if m else None
    m = re.search(r"cublas \+ softmax \+ cublas\s+[\d.]+ GFlop/s\s+([\d.]+) us", out)
    unfused = float(m.group(1)) / 1e3 if m else None
    torch_flash = torch_cudnn = torch_mem = None
    if VENV.exists():
        try:
            to = sh(f"{VENV} torch_bench.py {shape['QUERIES']} {shape['KEYS']} {shape['DEPTH']}", d,
                    log=LOGS / "flash_attention_torch.txt")
            torch_flash = us_row(to, "torch flash")
            torch_cudnn = us_row(to, "torch cudnn")
            torch_mem = us_row(to, "torch mem-efficient")
        except RuntimeError:
            pass
    bn, bt = best([("FlashAttention-2 (torch SDPA)", torch_flash), ("cuDNN SDPA (torch)", torch_cudnn),
                   ("torch memory-efficient SDPA", torch_mem), ("cuBLAS + softmax + cuBLAS", unfused)])
    amem = gpu_peak_mb(d, f"make -s {kv} HL_TARGET=host-cuda-profile bin/host-cuda-profile/runner && "
                          f"bin/host-cuda-profile/runner",
                       ["attention_flash", "attention_flash_rdom"], "flash_attention")
    return dict(ind_mb=amem.get("attention_flash"), rdom_mb=amem.get("attention_flash_rdom"),
                params=f"{shape['QUERIES']} queries x {shape['KEYS']} keys, depth {shape['DEPTH']}, fp16, "
                       f"chunk 64 (RDom {rdom_chunk}), RTX 5060 Ti",
                ind=ind, rdom=rdom, base_name=bn or "torch flash (unavailable)", base=bt)


SUITE = [
    ("cpu_biquads", lambda: cpu_biquads(False)),
    ("cpu_biquads", lambda: cpu_biquads(True)),
    ("cpu_rng", lambda: cpu_rng(False)),
    ("cpu_rng", lambda: cpu_rng(True)),
    ("cpu_alignment", lambda: cpu_alignment(False)),
    ("cpu_alignment", lambda: cpu_alignment(True)),
    ("viterbi", lambda: viterbi(16, 4, 320000)),
    ("viterbi", lambda: viterbi(64, 8, 50000)),
    ("viterbi", lambda: viterbi(8, 4, 16777216)),
    ("viterbi", lambda: viterbi(16, 4, 262144, NCORES, True)),
    ("ode", lambda: ode(1024, 1, 32768)),
    ("ode", lambda: ode(1024, NCORES, 8192, True)),
    # Rows past the last-level cache, so the materialized row is a trip
    # through memory: 256 MB rows serially, 32 MB rows over the cores.
    ("prefixsum", lambda: prefixsum(1 << 26, 2, 1)),
    ("prefixsum", lambda: prefixsum(1 << 23, 32, NCORES)),
    ("chebyshev", lambda: chebyshev(2048, 100)),
    ("cuda_mamba2", lambda: mamba2("fwd")),
    ("cuda_mamba2", lambda: mamba2("bwd")),
    ("flash_attention", flash_attention),
]


def fmt(ms):
    return "-" if ms is None else (f"{ms:.3f}" if ms < 1 else f"{ms:.1f}")


def fmt_mb(x):
    if x is None:
        return "-"
    if x < 1:
        return f"{x:.3g}"
    if x < 100:
        return f"{x:.1f}"
    return f"{x:.0f}"


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
    SCRATCH.mkdir(parents=True, exist_ok=True)
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

    hdr = ("| App | Config | Inductive (ms) | RDom (ms) | Ind peak (MB) | RDom peak (MB) | "
           "Fastest baseline (ms) | Baseline | Slower baselines tried | RDom / ind | Baseline / ind |")
    sep = "|---|---|---:|---:|---:|---:|---:|---|---|---:|---:|"
    lines = [hdr, sep]
    for r in rows:
        lines.append(f"| {r['app']} | {r['params']} | {fmt(r['ind'])} | {fmt(r['rdom'])} | "
                     f"{fmt_mb(r.get('ind_mb'))} | {fmt_mb(r.get('rdom_mb'))} | {fmt(r['base'])} | "
                     f"{r['base_name'] or '-'} | {', '.join(slower_tried(r['app'], r['base_name'])) or '-'} | "
                     f"{ratio(r['ind'], r['rdom'])} | {ratio(r['ind'], r['base'])} |")
    table = "\n".join(lines)
    print("\n" + table)
    notes = (
        "Times are milliseconds per run: inductive = the folded inductive-Func form, RDom = the same "
        "algorithm as update definitions (materializing), baseline = the fastest non-Halide implementation "
        "available on this machine, with the others tried listed beside it. Measured on a Threadripper 9970X "
        "(Zen 5, 32 cores in four 8-core chiplets) and an RTX 5060 Ti (driver 595.71, CUDA 13).\n\n"
        "The two peak-heap columns are the Halide profiler's memory_peak for each form, the high-water "
        "mark of the pipeline's internal heap (halide_malloc) during one run, read from the profiler's "
        "JSON report; input and output buffers, allocated by the harness, are not counted. This is what "
        "folding buys: the inductive form keeps a window of a few slices, so its intermediate heap is "
        "zero (biquads, rng: the window lives in registers) or just the part that cannot fold (alignment "
        "keeps the traceback direction plane), while the RDom form materializes the whole trajectory. On "
        "the GPU the profiler counts a pipeline's global device allocations too (device-only buffers, "
        "through its declare_allocation marker), so the number is the peak device memory the intermediates "
        "hold, not the input and output tensors: mamba2's inductive walk holds fewer chunk states than its "
        "RDom form (42 against 141 MB forward), and flash attention keeps its whole working set in "
        "tensor-core registers and shared memory, allocating no global intermediate, so it reads zero.\n\n"
        "Every form is verified against its app's reference before timing: the alignment rows byte-exact "
        "(score and CIGAR of every pair) against ksw2; the rng rows bit-exact against the scalar reference, "
        "and byte-exact against Julia's rand! in the eight-stream configuration (make LANES=8 test_julia); "
        "biquads against a double-precision cascade; the JIT apps against double references (viterbi's "
        "decoded path must match the reference decode except at near-ties, the prefix mean and ode to 1e-6 "
        "and 1e-5); the GPU rows against serial double references (mamba2's five gradients to 4e-3 and, "
        "for the step-size and decay gradients, 1e-3 of their largest value; attention against a double "
        "softmax with the same scale torch applies). The RDom forms leave their pure definitions undefined "
        "wherever the walk writes every element before it is read (biquads, rng, alignment, prefixsum, the "
        "materialized ode). The inductive forms do not need that, with one exception: ode's inductive form "
        "is inductive in the step but walks each slice as an update definition, since its folded observer's "
        "lane partial reads earlier elements of the same slice, so its two-slice window is left undefined "
        "too rather than zero-filled before every step.\n\n"
        "Threads: single-thread rows run pinned to one core. All-cores rows run one thread per physical core "
        "on the first hardware thread of each core, Halide forms and baselines alike: the materializing forms "
        "are bound by each chiplet's write path to the memory controller, and where the scheduler lands their "
        "threads otherwise moves them by up to 1.7x. The C++ baselines' parallel loops run on the same "
        "persistent Halide thread pool as the Halide forms (Finding Fast Filters, IPP, ksw2, parasail); "
        "oneTBB uses its own pool at the same thread count, Julia its own threads. Compiler flags are the "
        "same on both sides (-O3 -march=native -ffast-math, except Finding Fast Filters, whose authors "
        "found fast-math slower for it). Streaming stores are used only for the alignment direction plane, "
        "which is written once and read at 2N of its N^2 cells, in every form that has one; the rng and "
        "biquads outputs are consumed as they are produced and take ordinary stores in every "
        "implementation.\n\n"
        "The mamba2 rows compare each side at its own best chunk (Triton prefers 256; the Halide backward "
        "is best at 128), with the tensor-core schedules (WMMA=true), on inputs of the same layout (a "
        "head's sequence contiguous, which the Triton kernels take by its strides) and the same "
        "distributions; the Halide backward computes the "
        "step-size and decay gradients by the pair-sum path mamba_ssm uses, which costs it extra kernels "
        "that the cheaper adjoint identity would avoid at the price of a gradient that is wrong in float. "
        "Flash attention's RDom form is the same online softmax with the running maximum and row sum "
        "carried at the accumulator's shape, broadcast across its columns, so that one Tuple update over "
        "the key chunks advances all three (Halide fuses no dependent stages, and a per-row Func may not "
        "read the state its update feeds); the rescalings are then paid per element rather than per row, "
        "the same tile verbs and staging otherwise, so that row measures the cost of the expression, not "
        "of memory traffic. Chebyshev and the single-thread viterbi and ode rows are controls: chebyshev's "
        "trajectory fits in cache, and a lone recurrence on an otherwise idle machine has a chiplet's L3 and "
        "the whole write path to itself, so the materialized trajectory's extra bytes per step barely add "
        "to a step that is a dependency chain of a few dozen cycles (viterbi) or a microsecond of "
        "arithmetic (ode) even when they stream; folding buys nothing there. The all-cores viterbi and "
        "ode rows are the representative workload, many independent recurrences at once, where those "
        "bytes have to share the machine. The alignment rows are a matched comparison: the Halide "
        "forms are compiled for AVX2, the widest instruction set parasail ships kernels for (ksw2 ships SSE "
        "only); compiled for the machine's AVX-512 the single-thread row runs 1.4x faster again, while the "
        "all-cores row, at the memory wall, does not move (apps/cpu_alignment/README.md).\n\n"
        "Memory: the JIT apps and the alignment runner free their scratch at the end of every run, and past "
        "a few MB the default allocators hand it back to the kernel, so every process the driver runs, "
        "baselines included, uses jemalloc configured to keep freed memory (oversize_threshold:0, no decay) "
        "and to back it with transparent huge pages, which is what an application reusing its work buffers "
        "gets; Julia, which crashes under the preload, runs on its own allocator and retains its arrays "
        "itself. Every measurement, Halide forms and baselines in every language, follows one protocol "
        "(apps/support/bench_harness.h): three untimed runs, thirty timed trials, the best reported; on the "
        "GPU a trial is ten launches and one device synchronization, divided by ten. Baseline versions: "
        "torch 2.11.0+cu130 (FlashAttention-2 and cuDNN SDPA backends), Triton 3.6.0, mamba_ssm 2.3.2.post1, "
        "Julia 1.12.7, scipy 1.15.3, numpy 2.2.4.\n\n")
    Path(args.out).write_text(notes + table + "\n")
    print(f"\nwritten to {args.out}")


if __name__ == "__main__":
    main()
