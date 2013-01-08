#ifndef HALIDE_IR_MATCH_H
#define HALIDE_IR_MATCH_H

#include "IR.h"
#include <vector>

namespace Halide { 
namespace Internal {

// Does the first expression have the same structure as the second?
// Variables in the first expression with the name * are interpreted
// as wildcards, and their matching equivalent in the second
// expression is placed in the vector give as the third argument.
//
// E.g. with x being some Variable with name "*":
// match(x + x, 3 + (2*k), result) should return true, and set result to
// vec(3, 2*k)

bool expr_match(Expr pattern, Expr expr, std::vector<Expr> &result);
void expr_match_test();

}
}

#endif
