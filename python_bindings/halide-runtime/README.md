# halide-runtime

A tiny, standalone runtime for calling **precompiled Halide AOT kernels** from
Python, with **no dependency on libHalide** (no compiler, no LLVM).

```shell
pip install halide-runtime
uv add halide-runtime
```

```python
import numpy as np
import halide.runtime as hlr

kernel = hlr.load("mykernel.so")  # dlopen a precompiled Halide artifact
out = np.empty_like(inp)
kernel(inp, out)  # call it with NumPy arrays
```

This package contributes only `halide.runtime` — `hlr.load(...)`, the resulting
`Kernel` objects, and a lightweight `hlr.Buffer` wrapper — to the implicit
`halide` namespace. It is a strict subset of the full
[`halide`](https://pypi.org/project/halide/) package; install this one for small
deployment environments that run precompiled pipelines but do not need the
Halide compiler. Python extensions produced by
`add_halide_python_extension_library` also expose this package's `Kernel` type
and therefore require `halide-runtime` when deployed independently.

See the Halide Python docs:
<https://github.com/halide/Halide/blob/main/doc/Python.md>
