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
  A weight takes the first kernel in `GGML_HALIDE_MUL_MATS` for its type whose
  format's row group divides its rows, and is converted to that format when
  loaded (e.g. to GGML repack's interleaved `q4_K.8x8`, by the harness's
  `gq::relayout`; it can't be read back). Each MUL_MAT is one call of a kernel
  that takes f32 activations (`<weight>_f32_to_<act>_mul_mat`, quantization
  fused, e.g. into `q8_K.4x8`'s interleaved rows) on ggml-cpu's thread 0, over
  all the weight's rows; its parallel loops' tasks (`halide_do_par_for`, set
  globally) run on the MUL_MAT's other ggml-cpu threads, which wait for them in
  the op. Halide's own thread pool isn't used.
- `backend` (`ggml-halide-backend.cpp`): a backend like ggml-blas, with an ACCEL
  device, `HALIDE`, registered before BLAS. It runs the MUL_MATs of host weights
  (at any batch size) with the kernels that take f32 activations
  (`<weight>_f32_to_<act>_mul_mat`, with quantization fused), each as one call
  on Halide's thread pool (the app's copy, `../runtime`, with keep-awake held
  through each graph split). ggml's scheduler splits the graph between it and
  the CPU backend, so this measures what that costs.

Weight types without a kernel, and other ops, stay GGML's.

Metal isn't wired up yet. When it is, a weight with a compute-format library
(e.g. `q4_0:f16`, the weights rounded to f16 as GGML's `kernel_mul_mm`) should
dispatch on M per MUL_MAT as GGML does (mul_mv up to M = 16, then mul_mm): the
exact library (`q4_0_f32_mul_mat_metal`) for M \<= 16, the compute format's for
larger M. The harness's `halide-by-m-<name>` providers do this.

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
	-DGGML_HALIDE_KERNEL_DIR=$APPS_GGML_BUILD \
	-DGGML_HALIDE_INCLUDE_DIR=$HALIDE_INSTALL/include
cmake --build llama.cpp/build-hl
```

(`$APPS_GGML_BUILD`: a build of apps/ggml, with its kernels; `$HALIDE_INSTALL`:
Halide's.) The kernels used are listed as `weight:act` pairs in
`GGML_HALIDE_VEC_DOTS`, `GGML_HALIDE_MUL_MATS` and
`GGML_HALIDE_BACKEND_MUL_MATS` (default: q4_0, q8_0, q4_K, q5_K, q6_K and iq4_xs
with their GGML activation types). `GGML_HALIDE_MUL_MATS`' are formats (e.g.
`q4_K.8x8:q8_K.4x8`, the default for q4_K, then `q4_K:q8_K` for weights whose
rows aren't a multiple of 8).

## Checking

- `vec_dot` and `backend`: `test-backend-ops -o MUL_MAT`, with `-b HALIDE` for
  the backend (against the CPU), or another backend (e.g. `-b MTL0`) for
  `vec_dot` (the CPU, with the Halide kernels, is the reference).
- `mul_mat`: test-backend-ops doesn't use extra buffer types, so the weights
  need to be put in `HALIDE` explicitly and compared to the CPU's result
  (through `ggml_backend_dev_get_extra_bufts`).
