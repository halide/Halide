#ifndef HALIDE_SPECIALIZE_BRANCHED_LOOPS_H
#define HALIDE_SPECIALIZE_BRANCHED_LOOPS_H

/** \file
 * Defines a lowering pass that simplifies removes some branches from inside loops.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/**
 * Take a statement and check if it contains any IfThenElse nodes
 * nested inside any stack of For nodes. If so, check if we can
 * simplify the condition in the if statement to be a simple
 * inequality on one of the loop variables. Then we branch the loop
 * into two stages. This is then done recursively to handle nested if
 * statements.
 */
EXPORT Stmt specialize_branched_loops(Stmt s);

EXPORT void specialize_branched_loops_test();

}
}

#endif
