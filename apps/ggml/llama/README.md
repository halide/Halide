# The Halide kernels in llama.cpp

End-to-end runs of the app's kernels (an apps/ggml build's `*_bench` kernels) in
llama.cpp, three ways, each enabled at run time by the environment variable
`GGML_HALIDE` (a list, e.g. `GGML_HALIDE=vec_dot,mul_mat`; unset, llama.cpp is
unchanged):

- `vec_dot` (`ggml-halide-cpu.cpp`): ggml-cpu's vec_dot type traits become the
  Halide vec_dot kernels, still driven by ggml-cpu's mul_mat loop, threads and
  activation quantization. These types skip llamafile's sgemm, and their
  vec_dots do one row at a time (`nrows = 1`).
- `mul_mat` (`ggml-halide-cpu.cpp`): an extra buffer type of ggml-cpu, `HALIDE`,
  like its `CPU_REPACK`, and placed first so weights take it ahead of repack's.
  MUL_MATs of its weights run on ggml-cpu's threads: the activations are
  quantized in parallel (GGML's `from_float`), then each thread runs the Halide
  mul_mat kernel on chunks of the weight's rows, with Halide's own parallel
  loops serial. The kernels' output must be dense, so for more than one
  activation row, a chunk's output goes through scratch.
- `backend` (`ggml-halide-backend.cpp`): a backend like ggml-blas, with an ACCEL
  device, `HALIDE`, registered before BLAS. It runs the MUL_MATs of host weights
  (at any batch size) with the kernels that take f32 activations
  (`<weight>_f32_to_<act>_mul_mat`, with quantization fused), each as one call
  on Halide's thread pool (the app's copy, `../runtime`, with keep-awake held
  through each graph split). ggml's scheduler splits the graph between it and
  the CPU backend, so this measures what that costs.

Weight types without a kernel, and other ops, stay GGML's.

## Building

`llama.cpp.patch` applies to llama.cpp `e117148a41d8` (the llama.cpp sync of
ggml `d7cb574130e6`, the ggml that apps/ggml pins). It adds the cache variable
`GGML_CPU_HALIDE` (the path of `ggml-halide.cmake`) and its hooks to ggml-cpu,
ggml's backend registry and CMake.

```sh
git -C llama.cpp checkout e117148a41d8
git -C llama.cpp apply $HALIDE/apps/ggml/llama/llama.cpp.patch
cmake -S llama.cpp -B llama.cpp/build-hl -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DGGML_CPU_HALIDE=$HALIDE/apps/ggml/llama/ggml-halide.cmake \
	-DGGML_HALIDE_KERNEL_DIR= build \
	install <apps/ggml >-DGGML_HALIDE_INCLUDE_DIR= <Halide >/include
cmake --build llama.cpp/build-hl
```

The kernels used are listed as `weight:act` pairs in `GGML_HALIDE_VEC_DOTS`,
`GGML_HALIDE_MUL_MATS` and `GGML_HALIDE_BACKEND_MUL_MATS` (default: q4_0, q8_0,
q4_K, q6_K and iq4_xs with their GGML activation types).

## Checking

- `vec_dot` and `backend`: `test-backend-ops -o MUL_MAT`, with `-b HALIDE` for
  the backend (against the CPU), or another backend (e.g. `-b MTL0`) for
  `vec_dot` (the CPU, with the Halide kernels, is the reference).
- `mul_mat`: test-backend-ops doesn't use extra buffer types, so the weights
  need to be put in `HALIDE` explicitly and compared to the CPU's result
  (through `ggml_backend_dev_get_extra_bufts`).
