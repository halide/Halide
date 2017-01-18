#ifndef HALIDE_LOOP_CARRY_H
#define HALIDE_LOOP_CARRY_H

#include "Expr.h"

namespace Halide {
namespace Internal {

/** Reuse loads done on previous loop iterations by stashing them in
 * induction variables instead of redoing the load. If the loads are
 * predicated, the predicates need to match. Can be an optimization or
 * pessimization depending on how good the L1 cache is on the architecture
 * and how many memory issue slots there are. Currently only intended
 * for Hexagon. */
Stmt loop_carry(Stmt, int max_carried_values = 8);

}
}

#endif
