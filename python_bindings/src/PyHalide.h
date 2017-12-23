#ifndef HALIDE_PYTHON_BINDINGS_PYHALIDE_H
#define HALIDE_PYTHON_BINDINGS_PYHALIDE_H

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/operators.h>
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

// TODO: PyUtil.h?
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

template<typename T1, typename T2>
std::vector<std::pair<T1, T2>> args_to_pair_vector(const py::args &args, size_t start_offset = 0, size_t end_offset = 0) {
    if (args.size() < start_offset + end_offset) {
        throw py::value_error("Not enough arguments");
    }
    std::vector<std::pair<T1, T2>> v;
    v.reserve((args.size() - (start_offset + end_offset))/2);
    for (size_t i = start_offset; i < args.size() - end_offset; i += 2) {
        v.push_back({args[i].cast<T1>(), args[i+1].cast<T2>()});
    }
    return v;
}

}  // namespace PythonBindings
}  // namespace Halide


#endif  // HALIDE_PYTHON_BINDINGS_PYHALIDE_H
