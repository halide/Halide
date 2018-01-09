#include "PyInlineReductions.h"


#include "PyExpr.h"

namespace Halide {
namespace PythonBindings {

Expr sum0(Expr e, const std::string name) {
    return sum(e, name);
}

Expr sum1(RDom r, Expr e, const std::string name) {
    return sum(r, e, name);
}

Expr product0(Expr e, const std::string name) {
    return product(e, name);
}

Expr product1(RDom r, Expr e, const std::string name) {
    return product(r, e, name);
}

Expr maximum0(Expr e, const std::string name) {
    return maximum(e, name);
}

Expr maximum1(RDom r, Expr e, const std::string name) {
    return maximum(r, e, name);
}

Expr minimum0(Expr e, const std::string name) {
    return minimum(e, name);
}

Expr minimum1(RDom r, Expr e, const std::string name) {
    return minimum(r, e, name);
}

py::object argmin0(Expr e, const std::string name) {
    return expr_vector_to_python_tuple(argmin(e, name).as_vector());
}

py::object argmin1(RDom r, Expr e, const std::string name) {
    return expr_vector_to_python_tuple(argmin(r, e, name).as_vector());
}

py::object argmax0(Expr e, const std::string name) {
    return expr_vector_to_python_tuple(argmin(e, name).as_vector());
}

py::object argmax1(RDom r, Expr e, const std::string name) {
    return expr_vector_to_python_tuple(argmax(r, e, name).as_vector());
}

void define_inline_reductions() {
    // Defines some inline reductions: sum, product, minimum, maximum.

    py::def("sum", &sum0, (py::arg("e"), py::arg("name") = "sum"),
           "An inline reduction.");
    py::def("sum", &sum1, (py::arg("r"), py::arg("e"), py::arg("name") = "sum"),
           "An inline reduction.");

    py::def("product", &product0, (py::arg("e"), py::arg("name") = "product"),
           "An inline reduction.");
    py::def("product", &product1, (py::arg("r"), py::arg("e"), py::arg("name") = "product"),
           "An inline reduction.");

    py::def("maximum", &maximum0, (py::arg("e"), py::arg("name") = "maximum"),
           "An inline reduction.");
    py::def("maximum", &maximum1, (py::arg("r"), py::arg("e"), py::arg("name") = "maximum"),
           "An inline reduction.");

    py::def("minimum", &minimum0, (py::arg("e"), py::arg("name") = "minimum"),
           "An inline reduction.");
    py::def("minimum", &minimum1, (py::arg("r"), py::arg("e"), py::arg("name") = "minimum"),
           "An inline reduction.");

    py::def("argmin", &argmin0, (py::arg("e"), py::arg("name") = "argmin"),
           "An inline reduction.");
    py::def("argmin", &argmin1, (py::arg("r"), py::arg("e"), py::arg("name") = "argmin"),
           "An inline reduction.");

    py::def("argmax", &argmax0, (py::arg("e"), py::arg("name") = "argmax"),
           "An inline reduction.");
    py::def("argmax", &argmax1, (py::arg("r"), py::arg("e"), py::arg("name") = "argmax"),
           "An inline reduction.");
}

}  // namespace PythonBindings
}  // namespace Halide
