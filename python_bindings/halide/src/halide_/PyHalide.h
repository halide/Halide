#ifndef HALIDE_PYTHON_BINDINGS_PYHALIDE_H
#define HALIDE_PYTHON_BINDINGS_PYHALIDE_H

#include "PyBufferProtocol.h"
#include <nanobind/nanobind.h>
#include <nanobind/operators.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/unordered_set.h>
#include <nanobind/stl/variant.h>
#include <nanobind/stl/vector.h>

// Some very-commonly-used headers here, to simplify things.
#include <iostream>
#include <string>
#include <vector>

// Everyone needs Halide.h
#include "Halide.h"

namespace Halide {
namespace PythonBindings {

namespace py = nanobind;

template<typename T>
std::vector<T> args_to_vector(const py::object &value, size_t start_offset = 0, size_t end_offset = 0) {
    auto args = py::tuple(value);
    const size_t size = py::len(args);
    if (size < start_offset + end_offset) {
        throw py::value_error("Not enough arguments");
    }
    std::vector<T> v;
    v.reserve(size - (start_offset + end_offset));
    for (size_t i = start_offset; i < size - end_offset; ++i) {
        v.push_back(py::cast<T>(args[i]));
    }
    return v;
}

std::vector<Expr> collect_print_args(const py::tuple &args);
Expr double_to_expr_check(double v);
Target to_jit_target(const Target &target);
Target to_aot_target(const Target &target);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYHALIDE_H
