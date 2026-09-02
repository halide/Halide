# Times scipy.signal.sosfilt on the same shape and coefficients the runner
# uses. Usage: sosfilt_bench.py [channels samples sections]
import sys
import os
import time

import numpy as np
from scipy.signal import sosfilt

C, S, N = (int(a) for a in (sys.argv[1:4] + [32, 8 << 20, 8][len(sys.argv) - 1:]))

fs, q = 48000.0, 0.8
sos = np.zeros((N, 6))
for k in range(N):
    f0 = 120.0 * (8000.0 / 120.0) ** (k / (N - 1) if N > 1 else 0.0)
    gain_db = 3.0 if k % 2 else -3.0
    A = 10.0 ** (gain_db / 40)
    w0 = 2 * np.pi * f0 / fs
    alpha = np.sin(w0) / (2 * q)
    b = np.array([1 + alpha * A, -2 * np.cos(w0), 1 - alpha * A])
    a = np.array([1 + alpha / A, -2 * np.cos(w0), 1 - alpha / A])
    sos[k] = np.concatenate([b / a[0], a / a[0]])

x = np.empty((C, S), dtype=np.float32)
rng = np.random.default_rng(1234)
x[:] = rng.standard_normal((C, S), dtype=np.float32)

sos = sos.astype(np.float32)
# The shared protocol (apps/support/bench_harness.h): HB_WARMUP untimed
# runs, HB_TRIALS timed ones, the best reported.
for _ in range(int(os.environ.get("HB_WARMUP", 3))):
    y = sosfilt(sos, x, axis=-1)
best = float("inf")
for _ in range(int(os.environ.get("HB_TRIALS", 30))):
    t0 = time.perf_counter()
    y = sosfilt(sos, x, axis=-1)
    best = min(best, time.perf_counter() - t0)
print("  scipy.sosfilt %10.1f us  (dtype %s)" % (best * 1e6, y.dtype))
