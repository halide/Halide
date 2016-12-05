#ifndef HALIDE_REGISTER_PROMOTION_H
#define HALIDE_REGISTER_PROMOTION_H

#include "Expr.h"

namespace Halide {
namespace Internal {

/* Look for sequences of store -> ... loads and stores ... -> store to
 * the same address in a heap allocation. If no other stores or loads
 * in between might alias, promote all but the last store to a
 * temporary register instead. Helps for unrolled reductions. */
Stmt register_promotion(Stmt);

}
}

#endif 
