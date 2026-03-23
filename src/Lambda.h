#ifndef HALIDE_LAMBDA_H
#define HALIDE_LAMBDA_H

#include "Func.h"
#include "Var.h"

/** \file
 * Convenience functions for creating small anonymous Halide
 * functions. See test/lambda.cpp for example usage. */

namespace Halide {

/** Create a zero-dimensional halide function that returns the given
 * expression. The function may have more dimensions if the expression
 * contains implicit arguments. */
Func lambda(const Expr &e);

/** Create a 1-D halide function in the first argument that returns
 * the second argument. The function may have more dimensions if the
 * expression contains implicit arguments and the list of Var
 * arguments contains a placeholder ("_"). */
Func lambda(const Var &x, const Expr &e);

/** Create a 2-D halide function in the first two arguments that
 * returns the last argument. The function may have more dimensions if
 * the expression contains implicit arguments and the list of Var
 * arguments contains a placeholder ("_"). */
Func lambda(const Var &x, const Var &y, const Expr &e);

/** Create a 3-D halide function in the first three arguments that
 * returns the last argument.  The function may have more dimensions
 * if the expression contains implicit arguments and the list of Var
 * arguments contains a placeholder ("_"). */
Func lambda(const Var &x, const Var &y, const Var &z, const Expr &e);

/** Create a 4-D halide function in the first four arguments that
 * returns the last argument. The function may have more dimensions if
 * the expression contains implicit arguments and the list of Var
 * arguments contains a placeholder ("_"). */
Func lambda(const Var &x, const Var &y, const Var &z, const Var &w, const Expr &e);

/** Create a 5-D halide function in the first five arguments that
 * returns the last argument. The function may have more dimensions if
 * the expression contains implicit arguments and the list of Var
 * arguments contains a placeholder ("_"). */
Func lambda(const Var &x, const Var &y, const Var &z, const Var &w, const Var &v, const Expr &e);

}  // namespace Halide

#endif  // HALIDE_LAMBDA_H
