#include "PyStage.h"

#include "PyScheduleMethods.h"

namespace Halide {
namespace PythonBindings {

void define_stage(py::module &m) {
    auto stage_class =
        py::class_<Stage>(m, "Stage")
            // for implicitly_convertible
            .def(py::init([](const Func &f) -> Stage { return f; }))

            .def("dump_argument_list", &Stage::dump_argument_list)
            .def("name", &Stage::name)

            .def("rfactor", (Func(Stage::*)(std::vector<std::pair<RVar, Var>>)) & Stage::rfactor,
                 py::arg("preserved"))
            .def("rfactor", (Func(Stage::*)(const RVar &, const Var &)) & Stage::rfactor,
                 py::arg("r"), py::arg("v"));

    py::implicitly_convertible<Func, Stage>();

    add_schedule_methods(stage_class);
}

}  // namespace PythonBindings
}  // namespace Halide
