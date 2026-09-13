#pragma once

#include "kernel_registry.h"

// Registers GGML's own implementations into `registries`:
//
//   quantize / dequantize -- entirely via GGML's PUBLIC API
//   (ggml_get_type_traits / ggml_get_type_traits_cpu, see include/ggml.h and
//   include/ggml-cpu.h): `from_float_ref`/`to_float` (GGML's own documented
//   "reference" routines) become the Registry reference, and the
//   CPU-dispatched `from_float` becomes a candidate. No private header used.
//
//   vec_dot / repack (quantize_mat, gemv, gemm) -- these categories have no
//   public way to reach the always-available pure-C fallback, so the
//   `_generic`-suffixed symbol (declared in ggml_internal_abi.h) is
//   registered as the reference and the canonical symbol (reached publicly
//   for vec_dot via ggml_get_type_traits_cpu, and privately for repack,
//   which has no public accessor at all) is registered as a candidate.
//
// This is the only file in kernel-bench that knows GGML exists.
void register_ggml_provider(KernelRegistries &registries);
