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

            .def("rfactor", static_cast<Func (Stage::*)(const std::vector<std::pair<RVar, Var>> &)>(&Stage::rfactor),
                 py::arg("preserved"))
            .def("rfactor", static_cast<Func (Stage::*)(const RVar &, const Var &)>(&Stage::rfactor),
                 py::arg("r"), py::arg("v"))

            .def("split_vars", [](const Stage &stage) -> py::list {
                auto vars = stage.split_vars();
                py::list result;
                // Return a mixed-type list of Var and RVar objects, instead of
                // a list of VarOrRVar objects.
                for (const auto &v : vars) {
                    if (v.is_rvar) {
                        result.append(py::cast(v.rvar));
                    } else {
                        result.append(py::cast(v.var));
                    }
                }
                return result;
            })

            .def("unscheduled", &Stage::unscheduled);

    py::implicitly_convertible<Func, Stage>();

    add_schedule_methods(stage_class);
}

}  // namespace PythonBindings
}  // namespace Halide
