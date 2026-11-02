#include "PyVarOrRVar.h"

namespace Halide {
namespace PythonBindings {

void define_var_or_rvar(py::module_ &m) {
    py::class_<VarOrRVar>(m, "VarOrRVar")
        .def(py::init<std::string, bool>())
        .def(py::init<Var>())
        .def(py::init<RVar>())
        .def(py::init<RDom>())
        .def("name", &VarOrRVar::name)
        .def_ro("var", &VarOrRVar::var)
        .def_ro("rvar", &VarOrRVar::rvar)
        .def_ro("is_rvar", &VarOrRVar::is_rvar);

    py::implicitly_convertible<Var, VarOrRVar>();
    py::implicitly_convertible<RVar, VarOrRVar>();
    py::implicitly_convertible<RDom, VarOrRVar>();
}

}  // namespace PythonBindings
}  // namespace Halide
