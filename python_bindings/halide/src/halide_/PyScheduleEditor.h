#ifndef HALIDE_PYTHON_BINDINGS_PYSCHEDULEEDITOR_H
#define HALIDE_PYTHON_BINDINGS_PYSCHEDULEEDITOR_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_schedule_editor(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYSCHEDULEEDITOR_H
