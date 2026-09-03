#!/usr/bin/env python3
"""Times PyTorch's fused attention kernels at the Halide generator's shape:
one head, QUERIES x KEYS x DEPTH, fp16, no mask, no dropout, and the
default softmax scale of 1/sqrt(DEPTH), which is what the Halide forms
compute. Each of the FlashAttention-2, memory-efficient (xformers-style)
and cuDNN backends is timed on its own.

Usage: torch_bench.py QUERIES KEYS DEPTH
Prints the torch version, then one row per backend:
"  torch flash   <us> us", "  torch mem-efficient   <us> us",
"  torch cudnn   <us> us".
"""
import os
import sys

import torch
import torch.nn.functional as F
from torch.nn.attention import SDPBackend, sdpa_kernel

queries, keys, depth = (int(a) for a in sys.argv[1:4])
dev = "cuda"
torch.manual_seed(0)
q = torch.randn(1, 1, queries, depth, device=dev, dtype=torch.float16)
k = torch.randn(1, 1, keys, depth, device=dev, dtype=torch.float16)
v = torch.randn(1, 1, keys, depth, device=dev, dtype=torch.float16)


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


print(f"torch {torch.__version__}")
for name, backend in (("flash", SDPBackend.FLASH_ATTENTION),
                      ("mem-efficient", SDPBackend.EFFICIENT_ATTENTION),
                      ("cudnn", SDPBackend.CUDNN_ATTENTION)):
    try:
        with sdpa_kernel(backend):
            t = timed(lambda: F.scaled_dot_product_attention(q, k, v))
        print(f"  torch {name:14s} {t:10.1f} us")
    except RuntimeError as e:
        print(f"  torch {name:14s} unavailable: {str(e).splitlines()[0][:80]}")
