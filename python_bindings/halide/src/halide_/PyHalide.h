#ifndef HALIDE_PYTHON_BINDINGS_PYHALIDE_H
#define HALIDE_PYTHON_BINDINGS_PYHALIDE_H

#include <pybind11/numpy.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

// Some very-commonly-used headers here, to simplify things.
#include <iostream>
#include <string>
#include <vector>

// Everyone needs Halide.h
#include "Halide.h"

namespace Halide {
namespace PythonBindings {

namespace py = pybind11;

template<typename T>
std::vector<T> args_to_vector(const py::args &args, size_t start_offset = 0, size_t end_offset = 0) {
    if (args.size() < start_offset + end_offset) {
        throw py::value_error("Not enough arguments");
    }
    std::vector<T> v;
    v.reserve(args.size() - (start_offset + end_offset));
    for (size_t i = start_offset; i < args.size() - end_offset; ++i) {
        v.push_back(args[i].cast<T>());
    }
    return v;
}

std::vector<Expr> collect_print_args(const py::args &args);
Expr double_to_expr_check(double v);
Target to_jit_target(const Target &target);
Target to_aot_target(const Target &target);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYHALIDE_H
