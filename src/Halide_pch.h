// Precompiled header for the main (non-LLVM) Halide sources.
// This covers the standard library headers and core Halide headers that are
// included by the vast majority of translation units. Do not add LLVM headers
// here; those belong in Halide_llvm_pch.h together with the LLVM object library.
#ifndef HALIDE_PCH_H
#define HALIDE_PCH_H

// Standard library headers
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Core Halide headers included by nearly every TU
#include "Error.h"
#include "Expr.h"
#include "Type.h"

#endif  // HALIDE_PCH_H
