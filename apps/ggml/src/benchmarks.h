#pragma once

#include <vector>

#include "kernel_registry.h"
#include "report.h"

BenchReport run_quantize_benchmarks(const KernelRegistries &registries);
BenchReport run_dequantize_benchmarks(const KernelRegistries &registries);
BenchReport run_vecdot_benchmarks(const KernelRegistries &registries);

// One report each for quantize_mat, gemv, gemm.
std::vector<BenchReport> run_repack_benchmarks(const KernelRegistries &registries);
