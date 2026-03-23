#ifndef HALIDE_PYTHON_BINDINGS_PYERROR_H
#define HALIDE_PYTHON_BINDINGS_PYERROR_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_error(py::module &m);

struct PyJITUserContext : public JITUserContext {
    PyJITUserContext();
};

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYERROR_H
