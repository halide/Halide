#ifndef Z3_H
#define Z3_H

#include "Halide.h"

// Wrapper to use Z3 to do satisfiability queries on Halide Exprs

enum Z3Result {
    Sat, Unsat, Unknown
};

Z3Result satisfy(Halide::Expr constraint, std::map<std::string, Halide::Expr> *result);

#endif
