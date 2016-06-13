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

struct AssociativeOp {
	Expr op; // op(x, y)
	Expr identity;
	std::pair<std::string, Expr> x;
	std::pair<std::string, Expr> y;
};

/**
 * Given an update definition of a Func 'f', determine its equivalent associative
 * binary operator if there is any. The first boolean value of the returned pair
 * indicates if the operation was successfuly proven as associative. The second
 * vector contains the list of AssociativeOps for each Tuple element in the update
 * definition.
 *
 * For instance, f(x) = min(f(x), g(r.x)) will return true and it will also return
 * {{min(_x_0, _y_0), +inf, {_x_0, f(x)}, {_y_0, g(r.x)}}}, where the first Expr
 * is the equivalent binary operator, the second Expr is identity of the binary
 * operator, the third and the last pair contain the corresponding definition of
 * each variable in the binary operator. For unary operator, 'x' is not set.
 */
std::pair<bool, std::vector<AssociativeOp>> prove_associativity(
	const std::string &f, std::vector<Expr> args, std::vector<Expr> exprs);

EXPORT void associativity_test();

}
}

#endif
