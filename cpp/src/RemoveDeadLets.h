#ifndef HALIDE_REMOVE_DEAD_LETS_H
#define HALIDE_REMOVE_DEAD_LETS_H

/** \file
 * Defines the lowering pass that removes useless Let and LetStmt nodes
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Prune LetStmt and Let nodes that define variables that are never
 * used. Done as a final pass of lowering. */
Stmt remove_dead_lets(Stmt s);

}
}

#endif
