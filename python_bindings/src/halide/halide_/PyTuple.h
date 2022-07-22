#ifndef HALIDE_PYTHON_BINDINGS_PYTUPLE_H
#define HALIDE_PYTHON_BINDINGS_PYTUPLE_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_tuple(py::module &m);

// Templated function to allow for use with Realization as well as Tuple
template<typename T>
inline py::tuple to_python_tuple(const T &ht) {
    py::tuple pt(ht.size());
    for (size_t i = 0; i < ht.size(); i++) {
        pt[i] = py::cast(ht[i]);
    }
    return pt;
}

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYTUPLE_H
