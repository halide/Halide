#include "PyExpr.h"

#include <boost/format.hpp>
#include <boost/python.hpp>
#include <string>

#include "Halide.h"

#include "PyType.h"
#include "PyBinaryOperators.h"

namespace h = Halide;
namespace p = boost::python;

p::object expr_vector_to_python_tuple(const std::vector<h::Expr> &t) {
    if (t.size() == 1) {
        return p::object(t[0]);
    } else {
        p::list elts;
        for (const h::Expr &e : t) {
            elts.append(e);
        }
        return p::tuple(elts);
    }
}

std::vector<h::Expr> python_tuple_to_expr_vector(const p::object &obj) {
    p::extract<h::Expr> expr_extract(obj);
    if (expr_extract.check()) {
        return { expr_extract() };
    } else {
        return python_collection_to_vector<h::Expr>(obj);
    }
}

std::string expr_repr(const h::Expr &expr) {
    boost::format f("<halide.Expr of type %s>");
    return boost::str(f % halide_type_to_string(expr.type()));
}

h::Expr *expr_from_var_constructor(h::Var &var) {
    return new h::Expr(var);
}

void define_expr() {
    using Halide::Expr;

    auto expr_class = p::class_<Expr>("Expr",
                                      "An expression or fragment of Halide code.\n"
                                      "One can explicitly coerce most types to Expr via the Expr(x) constructor."
                                      "The following operators are implemented over Expr, and also other types"
                                      "such as Image, Func, Var, RVar generally coerce to Expr when used in arithmetic::\n\n"
                                      "+ - * / % ** & |\n"
                                      "-(unary) ~(unary)\n"
                                      " < <= == != > >=\n"
                                      "+= -= *= /=\n"
                                      "The following math global functions are also available::\n"
                                      "Unary:\n"
                                      "  abs acos acosh asin asinh atan atanh ceil cos cosh exp\n"
                                      "  fast_exp fast_log floor log round sin sinh sqrt tan tanh\n"
                                      "Binary:\n"
                                      "  hypot fast_pow max min pow\n\n"
                                      "Ternary:\n"
                                      "  clamp(x, lo, hi)                  -- Clamp expression to [lo, hi]\n"
                                      "  select(cond, if_true, if_false)   -- Return if_true if cond else if_false\n")

                          // constructor priority order is reverse from implicitly_convertible
                          // it important to declare int after float, after double.
                          .def(p::init<const h::Internal::BaseExprNode *>(p::arg("self")))
                          .def(p::init<double>(p::arg("self"), "Make an expression representing a const 32-bit float double. "
                                                               "Also emits a warning due to truncation."))
                          .def(p::init<float>(p::arg("self"), "Make an expression representing a const 32-bit float (i.e. a FloatImm)"))
                          .def(p::init<int>(p::arg("self"), "Make an expression representing a const 32-bit int (i.e. an IntImm)"))
                          .def(p::init<std::string>(p::arg("self"), "Make an expression representing a const string (i.e. a StringImm)"))
                          .def("__init__",
                               p::make_constructor(&expr_from_var_constructor, p::default_call_policies(),
                                                   p::arg("var")),
                               "Cast a Var into an Expr")

                          .def("type", &Expr::type, p::arg("self"),
                               "Get the type of this expression")
                          .def("__repr__", &expr_repr, p::arg("self"));
    ;

    add_binary_operators(expr_class);

    // implicitly_convertible declaration order matters,
    // int should be tried before float convertion
    p::implicitly_convertible<int, h::Expr>();
    p::implicitly_convertible<float, h::Expr>();
    p::implicitly_convertible<double, h::Expr>();

    p::enum_<h::DeviceAPI>("DeviceAPI")
        .value("None", h::DeviceAPI::None)
        .value("Host", h::DeviceAPI::Host)
        .value("Default_GPU", h::DeviceAPI::Default_GPU)
        .value("CUDA", h::DeviceAPI::CUDA)
        .value("OpenCL", h::DeviceAPI::OpenCL)
        .value("GLSL", h::DeviceAPI::GLSL)
        .value("OpenGLCompute", h::DeviceAPI::OpenGLCompute)
        .value("Metal", h::DeviceAPI::Metal)
        .value("Hexagon", h::DeviceAPI::Hexagon);
}
