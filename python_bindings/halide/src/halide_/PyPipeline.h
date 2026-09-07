#ifndef HALIDE_PYTHON_BINDINGS_PYPIPELINE_H
#define HALIDE_PYTHON_BINDINGS_PYPIPELINE_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_pipeline(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYPIPELINE_H
