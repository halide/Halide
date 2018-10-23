#ifndef HALIDE_PYTHON_BINDINGS_PYEXTERNFUNCARGUMENT_H
#define HALIDE_PYTHON_BINDINGS_PYEXTERNFUNCARGUMENT_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_extern_func_argument(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYEXTERNFUNCARGUMENT_H
