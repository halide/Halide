#include "Expr.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
#include "add_operators.h"
#include "no_compare_indexing_suite.h"

#include "../../src/Expr.h"
#include "../../src/Var.h"
#include "../../src/IROperator.h"

#include "Type.h"

#include <boost/format.hpp>
#include <string>

namespace h = Halide;
namespace p = boost::python;

std::string expr_repr(const h::Expr &expr)
{
    std::string repr;
    boost::format f("<halide.Expr of type '%s(%i)'>");

    const h::Type &t = expr.type();
    repr = boost::str(f % type_code_to_string(t) % t.bits());
    return repr;
}

h::Expr *expr_from_var_constructor(h::Var &var)
{
    return new h::Expr(var);
}

void defineExpr()
{
    using Halide::Expr;


    auto expr_class = p::class_<Expr>("Expr",
                                      "An expression or fragment of Halide code.\n" \
                                      "One can explicitly coerce most types to Expr via the Expr(x) constructor." \
                                      "The following operators are implemented over Expr, and also other types" \
                                      "such as Image, Func, Var, RVar generally coerce to Expr when used in arithmetic::\n\n" \
                                      "+ - * / % ** & |\n" \
                                      "-(unary) ~(unary)\n" \
                                      " < <= == != > >=\n" \
                                      "+= -= *= /=\n" \
                                      "The following math global functions are also available::\n" \
                                      "Unary:\n" \
                                      "  abs acos acosh asin asinh atan atanh ceil cos cosh exp\n" \
                                      "  fast_exp fast_log floor log round sin sinh sqrt tan tanh\n" \
                                      "Binary:\n" \
                                      "  hypot fast_pow max min pow\n\n" \
                                      "Ternary:\n" \
                                      "  clamp(x, lo, hi)                  -- Clamp expression to [lo, hi]\n" \
                                      "  select(cond, if_true, if_false)   -- Return if_true if cond else if_false\n")

            // constructor priority order is reverse from implicitly_convertible
            // it important to declare int after float, after double.
            .def(p::init<const h::Internal::BaseExprNode *>(p::arg("self"))) //Expr(const Internal::BaseExprNode *n) : IRHandle(n) {}
            .def(p::init<double>(p::arg("self"), "Make an expression representing a const 32-bit float double. "
                                 "Also emits a warning due to truncation."))
            .def(p::init<float>(p::arg("self"), "Make an expression representing a const 32-bit float (i.e. a FloatImm)"))
            .def(p::init<int>(p::arg("self"), "Make an expression representing a const 32-bit int (i.e. an IntImm)"))
            .def(p::init<std::string>(p::arg("self"), "Make an expression representing a const string (i.e. a StringImm)"))
            .def("__init__",
                 p::make_constructor(&expr_from_var_constructor, p::default_call_policies(),
                                     p::arg("var")), "Cast a Var into an Expr")


            .def("type", &Expr::type, p::arg("self"),
                 "Get the type of this expression")
            .def("__repr__", &expr_repr, p::arg("self"));
    ;

    add_operators(expr_class);

    // implicitly_convertible declaration order matters,
    // int should be tried before float convertion
    p::implicitly_convertible<int, h::Expr>();
    p::implicitly_convertible<float, h::Expr>();
    p::implicitly_convertible<double, h::Expr>();

    //p::implicitly_convertible<std::string, h::Expr>();

    p::class_< std::vector<Expr> >("ExprsVector")
            .def( no_compare_indexing_suite< std::vector<Expr> >() );

    p::enum_<h::DeviceAPI>("DeviceAPI",
                           "An enum describing a type of device API. "
                           "Used by schedules, and in the For loop IR node.")
            /// Used to denote for loops that inherit their device from where they are used, generally the default
            .value("Parent", h::DeviceAPI::Parent)
            .value("Host", h::DeviceAPI::Host)
            .value("Default_GPU", h::DeviceAPI::Default_GPU)
            .value("CUDA", h::DeviceAPI::CUDA)
            .value("OpenCL", h::DeviceAPI::OpenCL)
            .value("GLSL", h::DeviceAPI::GLSL)
            .value("Renderscript", h::DeviceAPI::Renderscript)
            .export_values()
            ;

    return;
}
