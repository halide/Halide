#ifndef HALIDE_VECTORIZE_SWAR_H
#define HALIDE_VECTORIZE_SWAR_H

/** \file
 * Defines the lowering pass that moves the vectors of a loop vectorized
 * with VectorizeIn::SWAR into general-purpose integer registers.
 */

#include "Expr.h"

#include <string>

namespace Halide {

struct Target;

namespace Internal {

/** Take the vectorized body of a loop scheduled with
 * VectorizeIn::SWAR (the output of vectorizing the loop named
 * loop_name), and rewrite all of its vector values as unsigned integer
 * scalars that hold the lanes packed together ("SIMD within a
 * register"). Lane i of a vector of b-bit lanes occupies bits [b * i, b
 * * (i + 1)) of the word. Dense vector loads and stores become
 * reinterprets of the loaded or stored value, which LLVM turns into
 * scalar loads and stores. Reports a user error for any vector that is
 * too wide for a general-purpose register of the target, or any
 * operation that cannot be done lane-wise within one. */
Stmt lower_swar_vectors(const Stmt &s, const std::string &loop_name, const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif
