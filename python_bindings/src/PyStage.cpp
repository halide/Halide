#include "PyStage.h"

#include "PyScheduleMethods.h"

namespace Halide {
namespace PythonBindings {

void define_stage(py::module &m) {
    auto stage_class =
        py::class_<Stage>(m, "Stage")
            .def("dump_argument_list", &Stage::dump_argument_list)
            .def("name", &Stage::name)

            .def("rfactor", (Func(Stage::*)(std::vector<std::pair<RVar, Var>>)) & Stage::rfactor,
                 py::arg("preserved"))
            .def("rfactor", (Func(Stage::*)(RVar, Var)) & Stage::rfactor,
                 py::arg("r"), py::arg("v"))

            // These two variants of compute_with are specific to Stage
            .def("compute_with", (Stage & (Stage::*)(LoopLevel, const std::vector<std::pair<VarOrRVar, LoopAlignStrategy>> &)) & Stage::compute_with,
                 py::arg("loop_level"), py::arg("align"))
            .def("compute_with", (Stage & (Stage::*)(LoopLevel, LoopAlignStrategy)) & Stage::compute_with,
                 py::arg("loop_level"), py::arg("align") = LoopAlignStrategy::Auto);
    add_schedule_methods(stage_class);
}

}  // namespace PythonBindings
}  // namespace Halide
