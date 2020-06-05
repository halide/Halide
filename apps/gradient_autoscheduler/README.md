This is a conservative autoscheduler that `compute_root` most Funcs except for
the trivial ones (think of it as a -O1 optimizer for Halide). It recognizes
large reduction patterns and use `rfactor` or `atomic` to parallelize on
associative reduction when there's not enough parallelism in the pure variable
domain. This strategy works reasonably well for gradient pipelines, and is
suitable as a default option for decent but not optimal performance. This is
also currently the only autoscheduler that generates GPU schedules.

Running some benchmarks in the app directory gives the following statistics (all
use `halide_reuse_device_allocations(nullptr, true)` for GPU)

| app              | manual (CPU) | gradient-autoscheduler (CPU) | manual (GPU) | gradient-autoscheduler (GPU) |
| ---------------- | ------------ | ---------------------------- | ------------ | ---------------------------- |
| bilateral filter | 7.93 ms      | 12.92 ms                     | 0.29 ms      | 1.05 ms                      |
| camera_pipe      | 8823.33 us   | 25126 us                     | 605.03 us    | 3347.44 us                   |
| lens_blur        | 7.77 ms      | 22.41 ms                     | 0.73 ms      | 5.60 ms                      |
| local_laplacian  | 42.29 ms     | 128.31 ms                    | 0.81 ms      | 14.30 ms                     |
| nl_means         | 145.003 ms   | out-of-memory                | N/A          | 82.93 ms                     |
| conv_layer       | 15.46 ms     | 6.89 ms                      | N/A          | 1.90 ms                      |
| stencil_chain    | 18.86 ms     | 21.46 ms                     | N/A          | 6.35 ms                      |

Tested on a 8 core Intel CPU (16 with HT) and TITAN Xp.

See `test.cpp` and `demo_generator.cpp` for how to use this autoscheduler. It
can also be used with Python bindings. Compile with

```
WITH_PYTHON=1 make
```

and see `test.py` for usage.
