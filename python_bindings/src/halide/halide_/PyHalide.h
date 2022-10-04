#ifndef HALIDE_PYTHON_BINDINGS_PYHALIDE_H
#define HALIDE_PYTHON_BINDINGS_PYHALIDE_H

#if HALIDE_USE_NANOBIND
#include <nanobind/nanobind.h>
#include <nanobind/operators.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>
#include <nanobind/tensor.h>
#else
#include <pybind11/numpy.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#endif

// Some very-commonly-used headers here, to simplify things.
#include <iostream>
#include <string>
#include <vector>

// Everyone needs Halide.h
#include "Halide.h"

namespace Halide {
namespace PythonBindings {

#if HALIDE_USE_NANOBIND
namespace py = nanobind;
#define HL_CAST(T, value) nanobind::cast<T>(value)
#else
namespace py = pybind11;
#define HL_CAST(T, value) ((value).cast<T>())
#endif

#if HALIDE_USE_NANOBIND
template<typename T>
std::vector<T> args_to_vector(const py::object &args_o, size_t start_offset = 0, size_t end_offset = 0) {
    py::args args = HL_CAST(py::args, args_o);
    if (args.size() < start_offset + end_offset) {
        throw py::value_error("Not enough arguments");
    }
    std::vector<T> v;
    v.reserve(args.size() - (start_offset + end_offset));
    for (size_t i = start_offset; i < args.size() - end_offset; ++i) {
        v.push_back(HL_CAST(T, args[i]));
    }
    return v;
}
#else
template<typename T>
std::vector<T> args_to_vector(const py::args &args, size_t start_offset = 0, size_t end_offset = 0) {
    if (args.size() < start_offset + end_offset) {
        throw py::value_error("Not enough arguments");
    }
    std::vector<T> v;
    v.reserve(args.size() - (start_offset + end_offset));
    for (size_t i = start_offset; i < args.size() - end_offset; ++i) {
        v.push_back(HL_CAST(T, args[i]));
    }
    return v;
}
#endif

std::vector<Expr> collect_print_args(const py::args &args);
Expr double_to_expr_check(double v);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYHALIDE_H
