# halide-runtime

A tiny, standalone runtime for calling **precompiled Halide AOT kernels** from
Python, with **no dependency on libHalide** (no compiler, no LLVM).

```python
import numpy as np
import halide.runtime as hlr

kernel = hlr.load("mykernel.so")        # dlopen a precompiled Halide artifact
out = np.empty_like(inp)
kernel(inp, out)                         # call it with NumPy arrays
```

This package provides only `halide.runtime` — `hlr.load(...)`, the resulting
`Kernel` objects, and a lightweight `hlr.Buffer` wrapper. It is a strict subset
of the full [`halide`](https://pypi.org/project/halide/) package; install this
one for small deployment environments that run precompiled pipelines but do not
need the Halide compiler. Accessing the compiler API (e.g. `halide.Func`) from a
runtime-only install raises a clear `ImportError` directing you to the full
`halide` package.

See the Halide Python docs:
<https://github.com/halide/Halide/blob/main/doc/Python.md>
