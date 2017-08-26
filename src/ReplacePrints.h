#ifndef HALIDE_REPLACE_PRINTS_H
#define HALIDE_REPLACE_PRINTS_H

/** \file
 * Defines the lowering pass that vectorizes loops marked as such
 */

#include "IR.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Take a statement with for loops marked for vectorization, and turn
 * them into single statements that operate on vectors. The loops in
 * question must have constant extent.
 */
Stmt replace_prints(Stmt s, const Target &t);

}
}

#endif
