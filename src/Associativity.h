#ifndef HALIDE_ASSOCIATIVITY_H
#define HALIDE_ASSOCIATIVITY_H

/** \file
 *
 * Methods for extracting an associative operator from a Func's update definition
 * if there is any and computing the identity of the associative operator.
 */

#include "IR.h"

#include <functional>

namespace Halide {
namespace Internal {

/**
 * Represent the equivalent associative binary/unary operator of an associative Expr.
 * For example, the following associative Expr, min(f(x), g(r.x) + 2), where f(x)
 * is the self-recurrence term, will be represented as:
 \code
 AssociativeOp assoc = {
        min(x, y),
        +inf,
        {"x", f(x)},
        {"y", g(r.x) + 2},
 };
 \endcode
 *
 * For unary operator, 'x' is not set, i.e. it will be a pair of empty string
 * and undefined Expr: {"", Expr()}. 'op' will only contain the 'y' term in
 * this case. For example, min(g(r.x), 4), will be represented as:
 \code
 AssociativeOp assoc = {
        y,
        0,
        {"", Expr()},
        {"y", min(g(r.x), 4)},
 };
 \endcode
 * Since it is a unary operator, the identity does not matter. It can be
 * anything.
 */
struct AssociativeOp {
        Expr op; // op(x, y)
        Expr identity;
        std::pair<std::string, Expr> x;
        std::pair<std::string, Expr> y;
};

/**
 * Given an update definition of a Func 'f', determine its equivalent associative
 * binary/unary operator if there is any. The first boolean value of the returned pair
 * indicates if the operation was successfuly proven as associative, and the second
 * vector contains the list of AssociativeOps for each Tuple element in the update
 * definition. If it fails to prove associativity, the second vector will be empty.
 *
 * For instance, f(x) = min(f(x), g(r.x)) will return true and it will also return
 * {{min(_x_0, _y_0), +inf, {_x_0, f(x)}, {_y_0, g(r.x)}}}, where the first Expr
 * is the equivalent binary operator, the second Expr is identity of the binary
 * operator, the third and the last pair contain the corresponding definition of
 * each variable in the binary operator.
 *
 * Note that even though f(x) = f(x) is associative, we'll treat it as non-associative
 * since it doesn't really make any sense to do any associative reduction on that
 * particular update definition.
 */
std::pair<bool, std::vector<AssociativeOp>> prove_associativity(
        const std::string &f, std::vector<Expr> args, std::vector<Expr> exprs);

EXPORT void associativity_test();

}
}

#endif
