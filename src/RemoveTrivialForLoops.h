#ifndef HALIDE_REMOVE_TRIVIAL_FOR_LOOPS_H
#define HALIDE_REMOVE_TRIVIAL_FOR_LOOPS_H

/** \file
 * Defines the lowering pass removes for loops of size 1
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Convert for loops of size 1 into LetStmt nodes, which allows for
 * further simplification. Done during a late stage of lowering. */
Stmt remove_trivial_for_loops(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
