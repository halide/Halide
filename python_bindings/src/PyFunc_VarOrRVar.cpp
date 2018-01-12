#include "PyFunc_VarOrRVar.h"

namespace Halide {
namespace PythonBindings {

void define_var_or_rvar() {
    py::class_<VarOrRVar>("VarOrRVar",
                         "A class that can represent Vars or RVars. "
                         "Used for reorder calls which can accept a mix of either.",
                         py::init<std::string, bool>(py::args("self", "n", "r")))
        .def(py::init<Var>(py::args("self", "v")))
        .def(py::init<RVar>(py::args("self", "r")))
        .def(py::init<RDom>(py::args("self", "r")))
        .def("name", &VarOrRVar::name, py::arg("self"), py::return_value_policy<py::copy_const_reference>())
        .def_readonly("var", &VarOrRVar::var)
        .def_readonly("rvar", &VarOrRVar::rvar)
        .def_readonly("is_rvar", &VarOrRVar::is_rvar);

    py::implicitly_convertible<Var, VarOrRVar>();
    py::implicitly_convertible<RVar, VarOrRVar>();
    py::implicitly_convertible<RDom, VarOrRVar>();
}

}  // namespace PythonBindings
}  // namespace Halide
