#ifndef SYNTHESIZE_PREDICATE_H
#define SYNTHESIZE_PREDICATE_H

#include "Halide.h"

bool can_disprove_nonconvex(Halide::Expr e, int beam_size, Halide::Expr *implication = nullptr);

// Takes a LHS and RHS with symbolic constants, and returns a
// sufficient condition on the constants for which lhs is known to
// equal rhs. Also returns expressions that give the value of any
// symbolic constants that only appear in the rhs.
Halide::Expr synthesize_predicate(const Halide::Expr &lhs,
                                  const Halide::Expr &rhs,
                                  std::map<std::string, Halide::Expr> *binding, int beam_size = 16);

#endif
