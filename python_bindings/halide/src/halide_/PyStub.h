#ifndef HALIDE_PYTHON_BINDINGS_PYSTUB_H
#define HALIDE_PYTHON_BINDINGS_PYSTUB_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_pystub(py::module_ &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif
