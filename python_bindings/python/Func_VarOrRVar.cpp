#include "Func_VarOrRVar.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
//#include "add_operators.h"

#include "Halide.h"

#include <string>
#include <vector>

void defineVarOrRVar() {
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

    return;
}
