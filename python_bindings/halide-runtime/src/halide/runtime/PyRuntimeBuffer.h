#ifndef HALIDE_PYTHON_RUNTIME_BUFFER_H
#define HALIDE_PYTHON_RUNTIME_BUFFER_H

#include <nanobind/nanobind.h>

#include "HalideRuntime.h"

namespace Halide::PythonRuntimeBindings {

#define HALIDE_PYTHON_STRINGIFY_IMPL(x) #x
#define HALIDE_PYTHON_STRINGIFY(x) HALIDE_PYTHON_STRINGIFY_IMPL(x)
inline constexpr char halide_buffer_capsule_name[] =
    "halide.halide_buffer_t.v" HALIDE_PYTHON_STRINGIFY(HALIDE_VERSION_MAJOR);
inline constexpr char runtime_buffer_capsule_name[] =
    "halide.runtime.Buffer.v" HALIDE_PYTHON_STRINGIFY(HALIDE_VERSION_MAJOR);
#undef HALIDE_PYTHON_STRINGIFY
#undef HALIDE_PYTHON_STRINGIFY_IMPL

void define_buffer(nanobind::module_ &m);

}  // namespace Halide::PythonRuntimeBindings

#endif
