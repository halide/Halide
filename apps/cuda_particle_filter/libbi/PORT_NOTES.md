# Porting LibBi 1.4.5 to CUDA 13 / Blackwell (sm_120)

LibBi (Murray 2015) is the reference implementation of the Metropolis resampler
used as a third-party baseline for the particle-filter benchmark. Its GPU path
predates several CUDA changes and does not build with a modern toolchain out of
the box. The stock `libbi filter --enable-cuda` fails; so does compiling its
generated tree unmodified.

## Why it broke

The fatal errors were **not** in LibBi's own device code (which uses plain
`<<<>>>` launches and the cuBLAS v2 handle API, both still valid). They came from:

1. **Bundled Thrust 1.8.2 (2015)** — its CUB uses the texture-reference API
   (`texture<>`, `cudaBindTexture`) and the legacy launch ABI
   (`cudaConfigureCall`), all removed in CUDA 12.
2. `thrust::identity` — removed in CCCL 2.x (5 use sites in bi::).
3. `thrust/detail/normal_iterator.h` — moved to `thrust/iterator/detail/`.
4. `cudaThreadSetCacheConfig` — removed; one call in `bi/init.hpp`.

LibBi uses only stable, public, high-level Thrust algorithms (transform, copy,
gather, scatter, lower_bound, extrema, reductions), so the fix is to drop the
bundled Thrust and build against CUDA 13's own CCCL Thrust with small shims.

## The fixes (all in `cuda13_shim/` + one framework line)

- `cuda13_shim/libbi_compat.hpp` — restores `thrust::identity` (force-included).
- `cuda13_shim/thrust/detail/normal_iterator.h` — forwards to the new location.
- `bi/init.hpp`: `cudaThreadSetCacheConfig` -> `cudaDeviceSetCacheConfig`.
- Build against `-I$CUDA/include/cccl`, define `HAVE_CBLAS_H`, add the system
  cblas include. `std::unary_function` is still provided by libstdc++ (no shim).
- Drive the build directly (see `build_gpu_cuda13.sh`): re-run `configure` with
  `--disable-dependency-tracking` and make LibBi's hand-added
  `include .../*_gpu.Po` lines optional (`-include`).

## Result

`filter_gpu` builds clean and runs on an RTX 5060 Ti (sm_120, CUDA 13.2),
producing a full filtered trajectory for T=1024, N=1024 particles.

## Data

`data.nc` is a fixed synthetic Gordon nonlinear time series (state `x` and
observations `y`, T=1024), used as the shared observation set for both the CPU
and GPU LibBi runs. `filter_gpu`/`filter_cpu` read `y` from it via `--obs-file`.
