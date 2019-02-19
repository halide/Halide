#ifndef HALIDE_LAMBDA_H
#define HALIDE_LAMBDA_H

#include "Func.h"
#include "Util.h"
#include "Var.h"

/** \file
 * Convenience functions for creating small anonymous Halide
 * functions. See test/lambda.cpp for example usage. */

namespace Halide {

/** Create a zero-dimensional halide function that returns the given
 * expression. The function may have more dimensions if the expression
 * contains implicit arguments. */
inline Func lambda(Expr e) {
    Func f("lambda" + Internal::unique_name('_'));
    f(_) = e;
    return f;
}

/** Create a 1-D halide function in the first argument that returns
 * the second argument. The function may have more dimensions if the
 * expression contains implicit arguments and the list of Var
 * arguments contains a placeholder ("_"). */
inline Func lambda(Var x, Expr e) {
    Func f("lambda" + Internal::unique_name('_'));
    f(x) = e;
    return f;
}

/** Create a 2-D halide function in the first two arguments that
 * returns the last argument. The function may have more dimensions if
 * the expression contains implicit arguments and the list of Var
 * arguments contains a placeholder ("_"). */
inline Func lambda(Var x, Var y, Expr e) {
    Func f("lambda" + Internal::unique_name('_'));
    f(x, y) = e;
    return f;
}

/** Create a 3-D halide function in the first three arguments that
 * returns the last argument.  The function may have more dimensions
 * if the expression contains implicit arguments and the list of Var
 * arguments contains a placeholder ("_"). */
inline Func lambda(Var x, Var y, Var z, Expr e) {
    Func f("lambda" + Internal::unique_name('_'));
    f(x, y, z) = e;
    return f;
}

/** Create a 4-D halide function in the first four arguments that
 * returns the last argument. The function may have more dimensions if
 * the expression contains implicit arguments and the list of Var
 * arguments contains a placeholder ("_"). */
inline Func lambda(Var x, Var y, Var z, Var w, Expr e) {
    Func f("lambda" + Internal::unique_name('_'));
    f(x, y, z, w) = e;
    return f;
}

/** Create a 5-D halide function in the first five arguments that
 * returns the last argument. The function may have more dimensions if
 * the expression contains implicit arguments and the list of Var
 * arguments contains a placeholder ("_"). */
inline Func lambda(Var x, Var y, Var z, Var w, Var v, Expr e) {
    Func f("lambda" + Internal::unique_name('_'));
    f(x, y, z, w, v) = e;
    return f;
}

}  // namespace Halide

#endif  //HALIDE_LAMBDA_H
