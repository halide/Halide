#ifndef IR_MATCH_H
#define IR_MATCH_H

#include "IR.h"
#include <map>
#include <string>

namespace Halide { 
namespace Internal {

// Does the first expression have the same structure as the
// second? Variables in the first expression are interpreted as
// wildcards, and their matching equivalent in the second expression
// is placed in the map given as the third argument.
//
// E.g. match(x + y, 3 + (2*k), env) should return true, and set
// env["x"] to 3 and env["y"] to 2*k

bool expr_match(Expr pattern, Expr expr, std::map<std::string, Expr> &);
void expr_match_test();

}
}

#endif
