#include "PyLambda.h"

namespace Halide {
namespace PythonBindings {

Func lambda0D(Expr e) {
    return lambda(e);
}

Func lambda1D(Var x, Expr e) {
    return lambda(x, e);
}

Func lambda2D(Var x, Var y, Expr e) {
    return lambda(x, y, e);
}

Func lambda3D(Var x, Var y, Var z, Expr e) {
    return lambda(x, y, z, e);
}

Func lambda4D(Var x, Var y, Var z, Var w, Expr e) {
    return lambda(x, y, z, w, e);
}

Func lambda5D(Var x, Var y, Var z, Var w, Var v, Expr e) {
    return lambda(x, y, z, w, v, e);
}

/// Convenience functions for creating small anonymous Halide functions.
/// See test/lambda.cpp for example usage.
/// lambda is a python keyword so we used lambda0D, lambda1D, ... lambda5D instead.
void define_lambda() {


    py::def("lambda0D", &lambda0D, py::arg("e"),
           "Create a zero-dimensional halide function that returns the given "
           "expression. The function may have more dimensions if the expression "
           "contains implicit arguments.");

    py::def("lambda1D", &lambda1D, py::args("x", "e"),
           "Create a 1-D halide function in the first argument that returns "
           "the second argument. The function may have more dimensions if the "
           "expression contains implicit arguments and the list of Var "
           "arguments contains a placeholder (\"_\").");

    py::def("lambda2D", &lambda2D, py::args("x", "y", "e"),
           "Create a 2-D halide function in the first two arguments that "
           "returns the last argument. The function may have more dimensions if "
           "the expression contains implicit arguments and the list of Var "
           "arguments contains a placeholder (\"_\").");

    py::def("lambda3D", &lambda3D, py::args("x", "y", "z", "e"),
           "Create a 3-D halide function in the first three arguments that "
           "returns the last argument.  The function may have more dimensions "
           "if the expression contains implicit arguments and the list of Var "
           "arguments contains a placeholder (\"_\").");

    py::def("lambda4D", &lambda4D, py::args("x", "y", "z", "w", "e"),
           "Create a 4-D halide function in the first four arguments that "
           "returns the last argument. The function may have more dimensions if "
           "the expression contains implicit arguments and the list of Var "
           "arguments contains a placeholder (\"_\").");

    py::def("lambda5D", &lambda5D, py::args("x", "y", "z", "w", "v", "e"),
           "Create a 5-D halide function in the first five arguments that "
           "returns the last argument. The function may have more dimensions if "
           "the expression contains implicit arguments and the list of Var "
           "arguments contains a placeholder (\"_\").");
}

}  // namespace PythonBindings
}  // namespace Halide
