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

// TODO: when we move to C++20 & our base toolchains are modern enough,
//   we can just use std::filesystem::path, since pybind11 has a built-in
//   type caster in <pybind11/stl/filesystem.h>.
class PathLike {
    std::string path;

public:
    PathLike() = default;
    PathLike(const py::bytes &path)
        : path(path) {
    }

    operator const std::string &() const {
        return path;
    }

    PyObject *decode() const {
        return PyUnicode_DecodeFSDefaultAndSize(path.c_str(), static_cast<ssize_t>(path.size()));
    }
};

}  // namespace PythonBindings
}  // namespace Halide

template<>
class pybind11::detail::type_caster<Halide::PythonBindings::PathLike> {
public:
    PYBIND11_TYPE_CASTER(Halide::PythonBindings::PathLike, const_name("os.PathLike"));

    bool load(handle src, bool) {
        try {
            PyObject *path = nullptr;
            if (!PyUnicode_FSConverter(src.ptr(), &path)) {
                throw error_already_set();
            }
            value = reinterpret_steal<bytes>(path);
            return true;
        } catch (error_already_set &) {
            return false;
        }
    }

    static handle cast(const Halide::PythonBindings::PathLike &path,
                       return_value_policy, handle) {
        if (auto *py_str = path.decode()) {
            return module_::import("pathlib")
                .attr("Path")(reinterpret_steal<object>(py_str))
                .release();
        }
        return nullptr;
    }
};

#endif  // HALIDE_PYTHON_BINDINGS_PYHALIDE_H
