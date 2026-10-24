#include "PyStage.h"

#include "PyScheduleMethods.h"
#include "PyTuple.h"

namespace Halide {
namespace PythonBindings {

namespace {

py::tuple stage_values(const Stage &stage) {
    return to_python_tuple(stage.values());
}

std::vector<Func> hoist_invariants_of_funcs(Stage &stage, const std::vector<Func> &funcs) {
    std::vector<std::vector<Func>> groups;
    for (const Func &f : funcs) {
        groups.push_back({f});
    }
    return stage.hoist_invariants(groups);
}

std::vector<Func> hoist_invariants_of_groups(Stage &stage, const std::vector<std::vector<Func>> &groups) {
    return stage.hoist_invariants(groups);
}

}  // namespace

void define_stage(py::module &m) {
    auto stage_class =
        py::class_<Stage>(m, "Stage")
            // for implicitly_convertible
            .def(py::init([](const Func &f) -> Stage { return f; }))

            .def("dump_argument_list", &Stage::dump_argument_list)
            .def("name", &Stage::name)
            .def("value", &Stage::value)
            .def("values", &stage_values)

            .def("stream_stores", &Stage::stream_stores)
            .def("stream_loads", static_cast<Stage &(Stage::*)()>(&Stage::stream_loads))
            .def("stream_loads", static_cast<Stage &(Stage::*)(const std::vector<Func> &)>(&Stage::stream_loads),
                 py::arg("funcs"))

            .def("rfactor", static_cast<Func (Stage::*)(const std::vector<std::pair<RVar, Var>> &)>(&Stage::rfactor),
                 py::arg("preserved"))
            .def("rfactor", static_cast<Func (Stage::*)(const RVar &, const Var &)>(&Stage::rfactor),
                 py::arg("r"), py::arg("v"))
            .def("hoist_invariants", &hoist_invariants_of_funcs, py::arg("funcs"))
            .def("hoist_invariants", &hoist_invariants_of_groups, py::arg("groups"))
            .def("hoist_invariants", [](Stage &stage) {
                return std::vector<Func>(stage.hoist_invariants());
            })

            .def("eager_inline", (Stage & (Stage::*)(const std::vector<Func> &)) & Stage::eager_inline, py::arg("fs"))
            .def("eager_inline", [](Stage &stage, const py::args &args) -> Stage & {
                return stage.eager_inline(args_to_vector<Func>(args));
            })

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
