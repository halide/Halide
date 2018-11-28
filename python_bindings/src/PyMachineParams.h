#ifndef HALIDE_PYTHON_BINDINGS_PYMACHINEPARAMS_H
#define HALIDE_PYTHON_BINDINGS_PYMACHINEPARAMS_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_machine_params(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYMACHINEPARAMS_H
