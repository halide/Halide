#include "PyFunc_VarOrRVar.h"

#include <boost/python.hpp>
#include <string>
#include <vector>

#include "Halide.h"

void define_var_or_rvar() {
    using Halide::VarOrRVar;
    namespace h = Halide;
    namespace p = boost::python;

    p::class_<VarOrRVar>("VarOrRVar",
                         "A class that can represent Vars or RVars. "
                         "Used for reorder calls which can accept a mix of either.",
                         p::init<std::string, bool>(p::args("self", "n", "r")))
        .def(p::init<h::Var>(p::args("self", "v")))
        .def(p::init<h::RVar>(p::args("self", "r")))
        .def(p::init<h::RDom>(p::args("self", "r")))
        .def("name", &VarOrRVar::name, p::arg("self"), p::return_value_policy<p::copy_const_reference>())
        .def_readonly("var", &VarOrRVar::var)
        .def_readonly("rvar", &VarOrRVar::rvar)
        .def_readonly("is_rvar", &VarOrRVar::is_rvar);

    p::implicitly_convertible<h::Var, VarOrRVar>();
    p::implicitly_convertible<h::RVar, VarOrRVar>();
    p::implicitly_convertible<h::RDom, VarOrRVar>();
}
