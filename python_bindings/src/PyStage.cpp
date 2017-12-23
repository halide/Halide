#include "PyStage.h"

#include "PyScheduleMethods.h"

namespace Halide {
namespace PythonBindings {

void define_stage(py::module &m) {
    auto stage_class =
        py::class_<Stage>(m, "Stage")
    ;
    add_schedule_methods(stage_class);
}

}  // namespace PythonBindings
}  // namespace Halide
