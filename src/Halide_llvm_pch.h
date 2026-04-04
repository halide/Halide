// Precompiled header for the LLVM-dependent Halide sources (Halide_LLVM_srcs
// object library). Extends the common PCH with LLVM_Headers.h.
#ifndef HALIDE_LLVM_PCH_H
#define HALIDE_LLVM_PCH_H

// LLVM umbrella header
#include "LLVM_Headers.h"

// Common standard library and core Halide headers (shared with non-LLVM sources).
#include "Halide_pch.h"

#endif  // HALIDE_LLVM_PCH_H
