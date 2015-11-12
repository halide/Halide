#ifndef HEXAGON_IR_CHECKER_H
#define HEXAGON_IR_CHECKER_H

/** \file
 * Defines the lowering pass that vectorizes loops marked as such
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Take a statement with for loops marked for vectorization, and turn
 * them into single statements that operate on vectors. The loops in
 * question must have constant extent.
 */
Stmt hexagon_ir_checker(Stmt);

/* Lowering pass for Hexagon
 */
Stmt hexagon_lower(Stmt s);
}
}

#endif
