#!/usr/bin/env python3
"""Times mamba_ssm's Triton Mamba-2 SSD kernel (mamba_chunk_scan_combined),
forward and backward, at the shape the Halide generator is built for.

Usage: triton_bench.py SEQ STATE CHANNELS HEADS GROUPS CHUNK
Prints "  triton fwd  <us> us" and "  triton bwd  <us> us" (the best sample, as the Halide runner reports).

The mamba_ssm package's __init__ imports its CUDA extension and several
model-zoo dependencies that are not needed for the Triton path; the
Triton modules are imported by path so none of that runs.
"""
import importlib
import os
import sys
import types

import torch

import importlib.util
pkg_dir = importlib.util.find_spec("mamba_ssm").submodule_search_locations[0]
for name, sub in (("mamba_ssm", ""), ("mamba_ssm.ops", "ops"),
                  ("mamba_ssm.ops.triton", "ops/triton")):
    m = types.ModuleType(name)
    m.__path__ = [os.path.join(pkg_dir, sub)]
    sys.modules[name] = m
ssd = importlib.import_module("mamba_ssm.ops.triton.ssd_combined")
mamba_chunk_scan_combined = ssd.mamba_chunk_scan_combined

seq, dstate, headdim, nheads, ngroups, chunk = (int(a) for a in sys.argv[1:7])
batch = 1
dev = "cuda"
torch.manual_seed(0)
x = torch.randn(batch, seq, nheads, headdim, device=dev, dtype=torch.float16, requires_grad=True)
dt = torch.rand(batch, seq, nheads, device=dev, dtype=torch.float32).requires_grad_()
A = (-torch.rand(nheads, device=dev, dtype=torch.float32)).requires_grad_()
B = torch.randn(batch, seq, ngroups, dstate, device=dev, dtype=torch.float16, requires_grad=True)
C = torch.randn(batch, seq, ngroups, dstate, device=dev, dtype=torch.float16, requires_grad=True)


def fwd():
    return mamba_chunk_scan_combined(x, dt, A, B, C, chunk, D=None)


def timed(fn):
    # The shared protocol (apps/support/bench_harness.h): HB_WARMUP untimed
    # trials, HB_TRIALS timed ones, each a batch of HB_BATCH launches and one
    # synchronize, the time divided by the batch; the best trial is reported.
    warm = int(os.environ.get("HB_WARMUP", 3))
    iters = int(os.environ.get("HB_TRIALS", 30))
    batch = int(os.environ.get("HB_BATCH", 10))
    for _ in range(warm):
        for _ in range(batch):
            fn()
        torch.cuda.synchronize()
    ts = []
    for _ in range(iters):
        s = torch.cuda.Event(enable_timing=True)
        e = torch.cuda.Event(enable_timing=True)
        s.record()
        for _ in range(batch):
            fn()
        e.record()
        torch.cuda.synchronize()
        ts.append(s.elapsed_time(e) * 1e3 / batch)
    return min(ts)


t_fwd = timed(fwd)
out = fwd()
dy = torch.randn_like(out)
inputs = (x, dt, A, B, C)


def bwd():
    torch.autograd.grad(out, inputs, grad_outputs=dy, retain_graph=True)


t_bwd = timed(bwd)
print(f"  triton fwd {t_fwd:12.1f} us  (mamba_chunk_scan_combined, chunk {chunk})")
print(f"  triton bwd {t_bwd:12.1f} us")
