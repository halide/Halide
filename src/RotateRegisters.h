#ifndef HALIDE_ROTATE_REGISTERS_H
#define HALIDE_ROTATE_REGISTERS_H

/** \file
 * Defines the lowering pass that rotates the contents of folded allocations
 * held in registers, so that they're only ever indexed by constants.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** A storage-folded realization is indexed by its coordinate modulo the fold
 * factor, which moves as the loops inside it advance. For a realization in
 * registers, move the values along by the amount each loop advances them at
 * the end of each of its iterations instead, so that every access uses an
 * index that's constant once unrolled loops have been unrolled. Must run after
 * storage folding and bound_small_allocations, and before storage
 * flattening. */
Stmt rotate_registers(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
