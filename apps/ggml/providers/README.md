# Adding a provider

A "provider" is anything that registers one or more implementations into a
`KernelRegistries` (see `include/kernel_registry.h`). `ggml_provider.cpp` is the
provider shipped today; it is the *only* file that knows GGML's internal symbols
exist. A new provider -- e.g. your own from-scratch reference implementation,
starting with dequantize per the project's stated goal -- is just another
translation unit that does the same thing.

## Steps

1. Create `providers/<name>_provider.h` declaring one function:

   ```cpp
   void register_<name>_provider(KernelRegistries & registries);
   ```

2. Create `providers/<name>_provider.cpp` implementing it. For each
   `(ggml_type, implementation)` pair you want benchmarked, call:

   ```cpp
   registries.dequantize.register_candidate(GGML_TYPE_Q4_0, "my-dequant", my_dequantize_q4_0);
   ```

   matching the category's function-pointer typedef from `kernel_registry.h`:

   | category                          | typedef           | signature                                                                                     |
   | --------------------------------- | ----------------- | --------------------------------------------------------------------------------------------- |
   | `quantize`, `repack_quantize_mat` | `quantize_fn_t`   | `(const float* x, void* y, int64_t k)`                                                        |
   | `dequantize`                      | `dequantize_fn_t` | `(const void* x, float* y, int64_t k)`                                                        |
   | `vec_dot`                         | `vec_dot_fn_t`    | `(int n, float* s, size_t bs, const void* vx, size_t bx, const void* vy, size_t by, int nrc)` |
   | `repack_gemv`, `repack_gemm`      | `gemx_fn_t`       | `(int n, float* s, size_t bs, const void* vx, const void* vy, int nr, int nc)`                |

   Use `register_candidate()` if GGML's existing reference should remain the
   correctness ground truth for that type (the common case: you want to see
   whether your implementation agrees with GGML and how fast it is). Use
   `register_reference()` instead if your implementation should *become* the new
   ground truth other candidates are compared against for that type -- the
   harness doesn't care which provider a reference comes from.

3. Add one line to `src/main.cpp`:

   ```cpp
   register_<name>_provider(registries);
   ```

   next to the existing `register_ggml_provider(registries);` call. Nothing else
   changes -- `bench_*.cpp`, the CLI, and the reporting code iterate whatever
   ends up in the registries and don't know or care how many providers
   contributed to them.

## Repack keys

`repack_quantize_mat`/`repack_gemv`/`repack_gemm` are keyed by `RepackKey` (base
type, activation type, interleave geometry, label string) rather than by
`ggml_type` alone, since several interleaved weight layouts can exist for the
same base type (e.g. `q4_0_4x4_q8_0` vs `q4_0_8x8_q8_0`). Reuse the `RepackKey`
values already registered by `ggml_provider.cpp` (see `k_repack_entries` in
`ggml_provider.cpp`) if you're providing an alternative gemv/gemm for an
existing layout; define your own `RepackKey` if you're introducing a new one.
