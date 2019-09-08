 #include "PyExpr.h"


#include "PyBinaryOperators.h"
#include "PyType.h"

namespace Halide {
namespace PythonBindings {

void define_expr(py::module &m) {
    auto to_bool = [](const Expr &e) -> bool {
        std::ostringstream o;
        o << e;
        throw py::value_error("The halide.Expr (" + o.str() + ") cannot be converted to a bool. "
            "If this error occurs using the 'and'/'or' keywords, consider using the '&'/'|' operators instead.");
        return false;
    };

    auto expr_class =
        py::class_<Expr>(m, "Expr")
            .def(py::init<>())
            // PyBind11 searches in declared order,
            // int should be tried before float conversion
            .def(py::init<int>())
            //.def(py::init<float>())
            .def(py::init<double>())
            .def(py::init<std::string>())

            // for implicitly_convertible
            .def(py::init([](const FuncRef &f) -> Expr { return f; }))
            .def(py::init([](const FuncTupleElementRef &f) -> Expr { return f; }))
            .def(py::init([](const Param<> &p) -> Expr { return p; }))
            .def(py::init([](const RDom &r) -> Expr { return r; }))
            .def(py::init([](const RVar &r) -> Expr { return r; }))
            .def(py::init([](const Var &v) -> Expr { return v; }))

            .def("__bool__", to_bool)
            .def("__nonzero__", to_bool)

            .def("type", &Expr::type)
            .def("__repr__", [](const Expr &e) -> std::string {
                std::ostringstream o;
                o << "<halide.Expr of type " << halide_type_to_string(e.type()) << ">";
                return o.str();
            })
    ;

    add_binary_operators(expr_class);

    // implicitly_convertible declaration order matters,
    // int should be tried before float conversion
    py::implicitly_convertible<int, Expr>();
    py::implicitly_convertible<float, Expr>();
    py::implicitly_convertible<double, Expr>();

    // There must be an Expr() ctor available for each of these
    py::implicitly_convertible<FuncRef, Expr>();
    py::implicitly_convertible<FuncTupleElementRef, Expr>();
    py::implicitly_convertible<Param<>, Expr>();
    py::implicitly_convertible<RDom, Expr>();
    py::implicitly_convertible<RVar, Expr>();
    py::implicitly_convertible<Var, Expr>();
}

}  // namespace PythonBindings
}  // namespace Halide
