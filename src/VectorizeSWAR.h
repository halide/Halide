#ifndef HALIDE_VECTORIZE_SWAR_H
#define HALIDE_VECTORIZE_SWAR_H

/** \file
 * Defines the lowering pass that moves the vectors of a loop vectorized
 * with VectorizeIn::SWAR into general-purpose integer registers.
 */

#include "ConstantInterval.h"
#include "Expr.h"
#include "Scope.h"

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
 * scalar loads and stores. The only vectors left are those, and lookups
 * of a word's 8-bit lanes in a table of constants: a dynamic_shuffle of
 * the word reinterpreted as its lanes, reinterpreted back as a word,
 * which targets with a byte permute in a general-purpose register (CUDA)
 * do there. Reports a user error for any vector that is
 * too wide for a general-purpose register of the target, or any
 * operation that cannot be done lane-wise within one. loop_bounds: the
 * constant bounds of the loops around s, which say which values its
 * scalars can take. */
Stmt lower_swar_vectors(const Stmt &s, const std::string &loop_name, const Target &target,
                        const Scope<ConstantInterval> &loop_bounds = Scope<ConstantInterval>::empty_scope());

/** Arithmetic on the lanes of vectors of integer type t packed as above into
 * words: unsigned integers of t.bits() * t.lanes() bits, which may be up to
 * 64. The operands and the results are words. */
// @{
/** The word with the low t.bits() bits of v in every lane. */
Expr swar_splat(const Type &t, uint64_t v);

/** The lane-wise sum and difference of a and b, wrapping in each lane. Each
 * operand appears twice in the result. */
Expr swar_add(const Type &t, const Expr &a, const Expr &b);
Expr swar_sub(const Type &t, const Expr &a, const Expr &b);

/** Every lane of w shifted by the constant c, in the other direction if c is
 * negative. A shift by at least the lane width fills the lane with zeros, or
 * with sign bits for a right shift of signed lanes. */
Expr swar_shift(const Type &t, const Expr &w, int64_t c, bool left);
// @}

}  // namespace Internal
}  // namespace Halide

#endif
