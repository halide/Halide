#include "Var.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
#include "add_operators.h"

#include "../../src/Var.h"
#include "../../src/IROperator.h"

#include <string>


void defineVar()
{
    using Halide::Var;

    namespace h = Halide;
    namespace p = boost::python;
    //using p::self;

    auto var_class = p::class_<Var>("Var",
                                    "A Halide variable, to be used when defining functions. It is just" \
                                    "a name, and can be reused in places where no name conflict will" \
                                    "occur. It can be used in the left-hand-side of a function" \
                                    "definition, or as an Expr. As an Expr, it always has type Int(32).\n" \
                                    "\n" \
                                    "Constructors::\n" \
                                    "Var()      -- Construct Var with an automatically-generated unique name\n" \
                                    "Var(name)  -- Construct Var with the given string name.\n",
                                    p::init<std::string>())
            .def(p::init<>())
            //.add_property("name", &Var::name) // "Get the name of a Var.")
            .def("name", &Var::name,
                 p::return_value_policy<p::copy_const_reference>(),
                 "Get the name of a Var.")
            .def("same_as", &Var::same_as, "Test if two Vars are the same.")
            //.def(self == p::other<Var>())
            .def("implicit", &Var::implicit, "Construct implicit Var from int n.");

    add_operators(var_class);

    p::implicitly_convertible<Var, h::Expr>();
    return;
}

