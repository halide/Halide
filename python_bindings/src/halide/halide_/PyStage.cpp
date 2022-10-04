#include "PyStage.h"

#include "PyScheduleMethods.h"

namespace Halide {
namespace PythonBindings {

void define_stage(py::module_ &m) {
    auto stage_class =
        py::class_<Stage>(m, "Stage")
#if HALIDE_USE_NANOBIND
            .def(py::init_implicit<Func>())
#else
            // for implicitly_convertible
            .def(py::init([](const Func &f) -> Stage { return f; }))
#endif
            .def("dump_argument_list", &Stage::dump_argument_list)
            .def("name", &Stage::name)

            .def("rfactor", (Func(Stage::*)(std::vector<std::pair<RVar, Var>>)) & Stage::rfactor,
                 py::arg("preserved"))
            .def("rfactor", (Func(Stage::*)(const RVar &, const Var &)) & Stage::rfactor,
                 py::arg("r"), py::arg("v"));

#if !HALIDE_USE_NANOBIND
    py::implicitly_convertible<Func, Stage>();
#endif

#if !HALIDE_USE_NANOBIND
    // TODO
    add_schedule_methods(stage_class);
#endif
}

}  // namespace PythonBindings
}  // namespace Halide
