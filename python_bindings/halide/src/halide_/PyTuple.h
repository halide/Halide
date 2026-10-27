#ifndef HALIDE_PYTHON_BINDINGS_PYTUPLE_H
#define HALIDE_PYTHON_BINDINGS_PYTUPLE_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_tuple(py::module_ &m);

// Templated function to allow for use with Realization as well as Tuple
template<typename T>
inline py::tuple to_python_tuple(const T &ht) {
    py::list pt;
    for (size_t i = 0; i < ht.size(); i++) {
        pt.append(ht[i]);
    }
    return py::tuple(pt);
}

// in: convertible-to-Expr, or tuple-of-convertible-to-Expr
// out: Halide::Tuple
// throws exception if not convertible
Tuple to_halide_tuple(const py::object &o);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYTUPLE_H
