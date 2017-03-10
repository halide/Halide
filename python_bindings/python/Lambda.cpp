#include "Lambda.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "Halide.h"

namespace h = Halide;

h::Func lambda0D(h::Expr e) {
    return lambda(e);
}

h::Func lambda1D(h::Var x, h::Expr e) {
    return lambda(x, e);
}

h::Func lambda2D(h::Var x, h::Var y, h::Expr e) {
    return lambda(x, y, e);
}

h::Func lambda3D(h::Var x, h::Var y, h::Var z, h::Expr e) {
    return lambda(x, y, z, e);
}

h::Func lambda4D(h::Var x, h::Var y, h::Var z, h::Var w, h::Expr e) {
    return lambda(x, y, z, w, e);
}

h::Func lambda5D(h::Var x, h::Var y, h::Var z, h::Var w, h::Var v, h::Expr e) {
    return lambda(x, y, z, w, v, e);
}

/// Convenience functions for creating small anonymous Halide functions.
/// See test/lambda.cpp for example usage.
/// lambda is a python keyword so we used lambda0D, lambda1D, ... lambda5D instead.
void defineLambda() {
    namespace p = boost::python;

    p::def("lambda0D", &lambda0D, p::arg("e"),
           "Create a zero-dimensional halide function that returns the given "
           "expression. The function may have more dimensions if the expression "
           "contains implicit arguments.");

    p::def("lambda1D", &lambda1D, p::args("x", "e"),
           "Create a 1-D halide function in the first argument that returns "
           "the second argument. The function may have more dimensions if the "
           "expression contains implicit arguments and the list of Var "
           "arguments contains a placeholder (\"_\").");

    p::def("lambda2D", &lambda2D, p::args("x", "y", "e"),
           "Create a 2-D halide function in the first two arguments that "
           "returns the last argument. The function may have more dimensions if "
           "the expression contains implicit arguments and the list of Var "
           "arguments contains a placeholder (\"_\").");

    p::def("lambda3D", &lambda3D, p::args("x", "y", "z", "e"),
           "Create a 3-D halide function in the first three arguments that "
           "returns the last argument.  The function may have more dimensions "
           "if the expression contains implicit arguments and the list of Var "
           "arguments contains a placeholder (\"_\").");

    p::def("lambda4D", &lambda4D, p::args("x", "y", "z", "w", "e"),
           "Create a 4-D halide function in the first four arguments that "
           "returns the last argument. The function may have more dimensions if "
           "the expression contains implicit arguments and the list of Var "
           "arguments contains a placeholder (\"_\").");

    p::def("lambda5D", &lambda5D, p::args("x", "y", "z", "w", "v", "e"),
           "Create a 5-D halide function in the first five arguments that "
           "returns the last argument. The function may have more dimensions if "
           "the expression contains implicit arguments and the list of Var "
           "arguments contains a placeholder (\"_\").");

    return;
}
