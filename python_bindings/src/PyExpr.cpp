#include "PyExpr.h"


#include "PyBinaryOperators.h"
#include "PyType.h"

namespace Halide {
namespace PythonBindings {

py::object expr_vector_to_python_tuple(const std::vector<Expr> &t) {
    if (t.size() == 1) {
        return py::object(t[0]);
    } else {
        py::list elts;
        for (const Expr &e : t) {
            elts.append(e);
        }
        return py::tuple(elts);
    }
}

std::vector<Expr> python_tuple_to_expr_vector(const py::object &obj) {
    py::extract<Expr> expr_extract(obj);
    if (expr_extract.check()) {
        return { expr_extract() };
    } else {
        return python_collection_to_vector<Expr>(obj);
    }
}

std::string expr_repr(const Expr &expr) {
    std::ostringstream o;
    o << "<halide.Expr of type " << halide_type_to_string(expr.type()) << ">";
    return o.str();
}

Expr *expr_from_var_constructor(Var &var) {
    return new Expr(var);
}

void define_expr() {
    auto expr_class = py::class_<Expr>("Expr",
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
                          .def(py::init<const Internal::BaseExprNode *>(py::arg("self")))
                          .def(py::init<double>(py::arg("self"), "Make an expression representing a const 32-bit float double. "
                                                               "Also emits a warning due to truncation."))
                          .def(py::init<float>(py::arg("self"), "Make an expression representing a const 32-bit float (i.e. a FloatImm)"))
                          .def(py::init<int>(py::arg("self"), "Make an expression representing a const 32-bit int (i.e. an IntImm)"))
                          .def(py::init<std::string>(py::arg("self"), "Make an expression representing a const string (i.e. a StringImm)"))
                          .def("__init__",
                               py::make_constructor(&expr_from_var_constructor, py::default_call_policies(),
                                                   py::arg("var")),
                               "Cast a Var into an Expr")

                          .def("type", &Expr::type, py::arg("self"),
                               "Get the type of this expression")
                          .def("__repr__", &expr_repr, py::arg("self"));
    ;

    add_binary_operators(expr_class);

    // implicitly_convertible declaration order matters,
    // int should be tried before float convertion
    py::implicitly_convertible<int, Expr>();
    py::implicitly_convertible<float, Expr>();
    py::implicitly_convertible<double, Expr>();

    py::enum_<DeviceAPI>("DeviceAPI")
        .value("None", DeviceAPI::None)
        .value("Host", DeviceAPI::Host)
        .value("Default_GPU", DeviceAPI::Default_GPU)
        .value("CUDA", DeviceAPI::CUDA)
        .value("OpenCL", DeviceAPI::OpenCL)
        .value("GLSL", DeviceAPI::GLSL)
        .value("OpenGLCompute", DeviceAPI::OpenGLCompute)
        .value("Metal", DeviceAPI::Metal)
        .value("Hexagon", DeviceAPI::Hexagon);
}

}  // namespace PythonBindings
}  // namespace Halide
