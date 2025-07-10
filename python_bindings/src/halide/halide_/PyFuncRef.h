#ifndef HALIDE_PYTHON_BINDINGS_PYFUNC_REF_H
#define HALIDE_PYTHON_BINDINGS_PYFUNC_REF_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

struct StageFromInPlaceUpdate {
    Stage new_stage;
    FuncRef func_ref;
};
void define_func_ref(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYFUNC_REF_H
