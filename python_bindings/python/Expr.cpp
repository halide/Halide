#include "Expr.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
#include "add_operators.h"
#include "no_compare_indexing_suite.h"

#include "../../src/Expr.h"
#include "../../src/IROperator.h"

#include <string>

void defineExpr()
{
    using Halide::Expr;

    namespace h = Halide;
    namespace p = boost::python;


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
                                      "  select(cond, if_true, if_false)   -- Return if_true if cond else if_false\n",
                                      p::init<std::string>(p::arg("self")))
            .def(p::init<int>(p::arg("self"))) // Make an expression representing a const 32-bit int (i.e. an IntImm)
            .def(p::init<float>(p::arg("self"))) // Make an expression representing a const 32-bit float (i.e. a FloatImm)
            .def(p::init<double>(p::arg("self"))) /* Make an expression representing a const 32-bit float, given a
                                                                                                                                                                                                                                                                                             * double. Also emits a warning due to truncation. */
            .def(p::init<std::string>(p::arg("self"))) // Make an expression representing a const string (i.e. a StringImm)
            .def(p::init<const h::Internal::BaseExprNode *>(p::arg("self"))) //Expr(const Internal::BaseExprNode *n) : IRHandle(n) {}
            .def("type", &Expr::type, p::arg("self")); // Get the type of this expression node

    add_operators(expr_class);

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
