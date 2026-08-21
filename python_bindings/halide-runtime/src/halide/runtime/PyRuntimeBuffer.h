#ifndef HALIDE_PYTHON_RUNTIME_BUFFER_H
#define HALIDE_PYTHON_RUNTIME_BUFFER_H

#include <pybind11/pybind11.h>

namespace Halide::PythonRuntimeBindings {

void define_buffer(pybind11::module_ &m);

}  // namespace Halide::PythonRuntimeBindings

#endif
