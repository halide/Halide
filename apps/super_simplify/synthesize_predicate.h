#ifndef SYNTHESIZE_PREDICATE_H
#define SYNTHESIZE_PREDICATE_H

#include "Halide.h"

// Takes a LHS and RHS with symbolic constants, and a list of example
// bindings for the constants for which LHS has been proved to equal
// RHS, and returns a sufficient condition on the constants for which
// lhs is known to equal rhs. Also returns expressions that give the
// value of any symbolic constants that only appear in the rhs.
Halide::Expr synthesize_predicate(const Halide::Expr &lhs,
                                  const Halide::Expr &rhs,
                                  const std::vector<std::map<std::string, Halide::Expr>> &examples,
                                  std::map<std::string, Halide::Expr> *binding);

#endif
