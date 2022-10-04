#include "PyVarOrRVar.h"

namespace Halide {
namespace PythonBindings {

void define_var_or_rvar(py::module_ &m) {
    py::class_<VarOrRVar>(m, "VarOrRVar")
        .def(py::init<std::string, bool>())
#if HALIDE_USE_NANOBIND
            .def(py::init_implicit<Var>())
            .def(py::init_implicit<RVar>())
            .def(py::init_implicit<RDom>())
#else
        .def(py::init<Var>())
        .def(py::init<RVar>())
        .def(py::init<RDom>())
#endif
        .def("name", &VarOrRVar::name)
        .def_readonly("var", &VarOrRVar::var)
        .def_readonly("rvar", &VarOrRVar::rvar)
        .def_readonly("is_rvar", &VarOrRVar::is_rvar);

#if !HALIDE_USE_NANOBIND
    py::implicitly_convertible<Var, VarOrRVar>();
    py::implicitly_convertible<RVar, VarOrRVar>();
    py::implicitly_convertible<RDom, VarOrRVar>();
#endif
}

}  // namespace PythonBindings
}  // namespace Halide
