#include "PyFunc_gpu.h"

namespace Halide {
namespace PythonBindings {

void define_func_gpu_methods(py::class_<Func> &func_class) {
    define_func_or_stage_gpu_methods<Func>(func_class);
}

}  // namespace PythonBindings
}  // namespace Halide
