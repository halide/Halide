#pragma once

#include "kernel_registry.h"

// Registers the from-scratch Halide reimplementation of GGML's Q4_0
// quantize/dequantize kernels (see ../halide/) as candidates against GGML's
// own reference, which register_ggml_provider() registers first.
void register_halide_provider(KernelRegistries &registries);
