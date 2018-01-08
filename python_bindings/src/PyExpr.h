#ifndef HALIDE_PYTHON_BINDINGS_PYEXPR_H
#define HALIDE_PYTHON_BINDINGS_PYEXPR_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_expr();

py::object expr_vector_to_python_tuple(const std::vector<Expr> &t);
std::vector<Expr> python_tuple_to_expr_vector(const py::object &obj);

template <typename T>
std::vector<T> python_collection_to_vector(const py::object &obj) {
    std::vector<T> result;
    for (ssize_t i = 0; i < py::len(obj); i++) {
        result.push_back(py::extract<T>(obj[i]));
    }
    return result;
}

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYEXPR_H
