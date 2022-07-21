#ifndef HALIDE_PYTHON_BINDINGS_PYMACHINEPARAMS_H
#define HALIDE_PYTHON_BINDINGS_PYMACHINEPARAMS_H

#ifdef HALIDE_ALLOW_LEGACY_AUTOSCHEDULER_API
#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_machine_params(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif
#endif  // HALIDE_PYTHON_BINDINGS_PYMACHINEPARAMS_H
