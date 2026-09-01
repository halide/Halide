# Times numpy filling the same-shaped float32 buffer from its default
# stateful generator (PCG64: 128-bit state plus increment per stream).
import sys
import time

import numpy as np

L, T = (int(a) for a in (sys.argv[1:3] + [32, 4 << 20][len(sys.argv) - 1:]))
rng = np.random.default_rng(1234)
rng.random((2, 4096), dtype=np.float32)
best = float("inf")
for _ in range(3):
    t0 = time.perf_counter()
    a = rng.random((L, T), dtype=np.float32)
    best = min(best, time.perf_counter() - t0)
print("  numpy PCG64 %10.1f us  (dtype %s)" % (best * 1e6, a.dtype))
