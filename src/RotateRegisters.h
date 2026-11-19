#ifndef HALIDE_ROTATE_REGISTERS_H
#define HALIDE_ROTATE_REGISTERS_H

/** \file
 * Defines the lowering pass that rotates the contents of folded allocations
 * held in registers, so that they're only ever indexed by constants.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** A storage-folded allocation is indexed by its coordinate modulo the fold
 * factor, which moves as the loops inside it advance. For an allocation in
 * registers, move the values along by the amount each loop advances them at
 * the end of each of its iterations instead, so that every load and store uses
 * a constant index. The values move in the same vectors the stores use. Must
 * run after storage flattening, unrolling, and vectorization. */
Stmt rotate_registers(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif
