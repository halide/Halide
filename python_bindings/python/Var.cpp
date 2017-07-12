#include "Var.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include "add_operators.h"
#include <boost/python.hpp>

#include "Halide.h"

#include <boost/format.hpp>
#include <string>

namespace h = Halide;

bool var_is_implicit0(h::Var &that) {
    return that.is_implicit();
}

bool var_is_implicit1(const std::string name) {
    return h::Var::is_implicit(name);
}

int var_implicit_index0(h::Var &that) {
    return that.is_implicit();
}

int var_implicit_index1(const std::string name) {
    return h::Var::is_implicit(name);
}

bool var_is_placeholder0(h::Var &that) {
    return that.is_placeholder();
}

bool var_is_placeholder1(const std::string name) {
    return h::Var::is_placeholder(name);
}

h::Expr var_as_expr(h::Var &that) {
    return static_cast<h::Expr>(that);
}

std::string var_repr(const h::Var &var) {
    std::string repr;
    boost::format f("<halide.Var '%s'>");
    repr = boost::str(f % var.name());
    return repr;
}

void defineVar() {
    using Halide::Var;

    namespace p = boost::python;

    auto var_class = p::class_<Var>("Var",
                                    "A Halide variable, to be used when defining functions. It is just"
                                    "a name, and can be reused in places where no name conflict will"
                                    "occur. It can be used in the left-hand-side of a function"
                                    "definition, or as an Expr. As an Expr, it always has type Int(32).\n"
                                    "\n"
                                    "Constructors::\n"
                                    "Var()      -- Construct Var with an automatically-generated unique name\n"
                                    "Var(name)  -- Construct Var with the given string name.\n",
                                    p::init<std::string>(p::args("self", "name")))
                         .def(p::init<>(p::arg("self")))
                         //.add_property("name", &Var::name) // "Get the name of a Var.")
                         .def("name", &Var::name, p::arg("self"),
                              p::return_value_policy<p::copy_const_reference>(),
                              "Get the name of a Var.")
                         .def("same_as", &Var::same_as, p::args("self", "other"), "Test if two Vars are the same.")
                         .def("__eq__", &Var::same_as, p::args("self", "other"), "Test if two Vars are the same.")
                         //.def(self == p::other<Var>())

                         .def("implicit", &Var::implicit, p::arg("n"),
                              "Implicit var constructor. Implicit variables are injected "
                              "automatically into a function call if the number of arguments "
                              "to the function are fewer than its dimensionality and a "
                              "placeholder (\"_\") appears in its argument list. Defining a "
                              "function to equal an expression containing implicit variables "
                              "similarly appends those implicit variables, in the same order, "
                              "to the left-hand-side of the definition where the placeholder "
                              "('_') appears.")
                         .staticmethod("implicit")
                         .def("is_implicit", &var_is_implicit0, p::arg("self"),
                              "Return whether the variable name is of the form for an implicit argument.")
                         .def("name_is_implicit", &var_is_implicit1, p::arg("name"),
                              "Return whether a variable name is of the form for an implicit argument.")
                         .staticmethod("name_is_implicit")

                         .def("implicit_index", &var_implicit_index0, p::arg("self"),
                              "Return the argument index for a placeholder argument given its "
                              "name. Returns 0 for \\ref _0, 1 for \\ref _1, etc. "
                              "Returns -1 if the variable is not of implicit form. ")
                         .def("name_implicit_index", &var_implicit_index1, p::arg("name"),
                              "Return the argument index for a placeholder argument given its "
                              "name. Returns 0 for \\ref _0, 1 for \\ref _1, etc. "
                              "Returns -1 if the variable is not of implicit form. ")
                         .staticmethod("name_implicit_index")

                         .def("is_placeholder", &var_is_placeholder0, p::arg("self"),
                              "Test if a var is the placeholder variable \\ref _")
                         .def("name_is_placeholder", &var_is_placeholder1, p::arg("name"),
                              "Test if a var is the placeholder variable \\ref _")
                         .staticmethod("name_is_placeholder")

                         .def("expr", &var_as_expr, p::arg("self"),  //operator Expr() const
                              "A Var can be treated as an Expr of type Int(32)")

                         .def("gpu_blocks", &Var::gpu_blocks,  // no args
                              "Vars to use for scheduling producer/consumer pairs on the gpu.")
                         .staticmethod("gpu_blocks")
                         .def("gpu_threads", &Var::gpu_threads,  // no args
                              "Vars to use for scheduling producer/consumer pairs on the gpu.")
                         .staticmethod("gpu_threads")

                         .def("outermost", &Var::outermost,  // no args
                              "A Var that represents the location outside the outermost loop.")
                         .staticmethod("outermost")

                         .def("__repr__", &var_repr, p::arg("self"));
    ;

    add_operators(var_class);
    add_operators_with<decltype(var_class), h::Expr>(var_class);

    p::implicitly_convertible<Var, h::Expr>();

    return;
}
