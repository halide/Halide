#ifndef INDUCTIVE_H
#define INDUCTIVE_H

/** \file
 *
 * Utilities for processing inductively defined functions.
 *
 * A simple example of an inductively defined function is
 * f(x) = select(x <= 0, input(0), input(x) + f(x - 1));
 * The purpose of inductive functions is to allow execution patterns that are
 * impossible with reduction domains. For example, in the following code:
 *
 * f(x) = select(x <= 0, input(0), input(x) + f(x - 1));
 * g(x) = f(x) / 4;
 * f.compute_at(g, x).store_root();
 *
 * The resulting program computes a single value of f(x) at each value of g(x),
 * thanks to Halide's sliding window optimization. As a result of storage folding,
 * only the two most recent values of f(x) are stored at any given time. This is
 * impossible if f(x) is defined using a reduction domain, since every value of f(x)
 * must be computed and stored before g(x) is computed.
 *
 * If Halide is unable to perform the sliding window optimization, computing the
 * inductive function is generally inefficient.
 *
 * In inductive functions, any recursive references must be inside a select statement,
 * and cannot be inside nested select statements. The inductive arguments in the
 * recursive reference must be monotonically decreasing. Currently, only single-valued
 * functions are supported. Inductive functions cannot be inlined.
 *
 * An inductive function is usually expressed as a single pure definition (as in the
 * examples above), but this is not required. It is also possible to define an inductive
 * function with a non-inductive pure definition plus exactly one update definition, where
 * the update definition is the inductive one. In that case, all RVars appearing in the
 * update definition must be nested inside all of the inductive variables.
 *
 * In some cases, the inductive function's type cannot be inferred and must be declared
 * explicitly. This occurs when constants appear in operations with a recursive reference.
 * For example, in the following code, Halide cannot infer the type of f:
 * f(x) = select(x <= 0, 0, f(x - 1) + 1);
 *
 * To fix this, declare f with an explicit type:
 * Func f = Func(Int(32), "f");
 */

#include "Bounds.h"
#include "Expr.h"
#include "Interval.h"
#include "Scope.h"
#include "Solve.h"

namespace Halide {
namespace Internal {

/** Given an initial box for an inductively defined function,
    returns an expanded box that includes the function's non-inductive base case.
    is_inductive_var[i] should be true iff vars[i] is a dimension in which the
    function actually recurses. */
Box expand_to_include_base_case(const std::vector<std::string> &vars, const std::vector<bool> &is_inductive_var,
                                const Expr &RHS, const std::string &func, const Box &box_required,
                                bool is_update = false);

}  // namespace Internal
}  // namespace Halide

#endif
