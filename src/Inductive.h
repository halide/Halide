#ifndef INDUCTIVE_H
#define INDUCTIVE_H

#include "Bounds.h"
#include "Expr.h"
#include "Interval.h"
#include "Scope.h"
#include "Solve.h"

namespace Halide {
namespace Internal {

/** Given an initial box for an inductively defined function, 
    returns an expanded box that includes the function's non-inductive base case. */
const Box expand_to_include_base_case(const std::vector<std::string> &vars, const Expr &RHS, const std::string &func, const Box &box_required);

const Box expand_to_include_base_case(const Function &fn, const Box &box_required, const int &pos=0);

const Box expand_to_include_base_case(const Function &fn, const Box &box_required);

} // namespace Internal
} // namespace Halide

#endif
