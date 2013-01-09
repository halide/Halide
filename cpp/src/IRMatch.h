#ifndef HALIDE_IR_MATCH_H
#define HALIDE_IR_MATCH_H

/** \file
 * Defines a method to match a fragment of IR against a pattern containing wildcards 
 */

#include "IR.h"
#include <vector>

namespace Halide { 
namespace Internal {

/** Does the first expression have the same structure as the second?
 * Variables in the first expression with the name * are interpreted
 * as wildcards, and their matching equivalent in the second
 * expression is placed in the vector give as the third argument.
 *
 * For example:
 \code
 Expr x = new Variable(Int(32), "*");
 match(x + x, 3 + (2*k), result) 
 \endcode
 * should return true, and set result[0] to 3 and
 * result[1] to 2*k.
 */

bool expr_match(Expr pattern, Expr expr, std::vector<Expr> &result);
void expr_match_test();

}
}

#endif
