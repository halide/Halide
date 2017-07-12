#include "InlineReductions.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "Halide.h"

#include "Expr.h"

#include <string>

namespace h = Halide;
namespace p = boost::python;

h::Expr sum0(h::Expr e, const std::string name) {
    return h::sum(e, name);
}

h::Expr sum1(h::RDom r, h::Expr e, const std::string name) {
    return h::sum(r, e, name);
}

h::Expr product0(h::Expr e, const std::string name) {
    return h::product(e, name);
}

h::Expr product1(h::RDom r, h::Expr e, const std::string name) {
    return h::product(r, e, name);
}

h::Expr maximum0(h::Expr e, const std::string name) {
    return h::maximum(e, name);
}

h::Expr maximum1(h::RDom r, h::Expr e, const std::string name) {
    return h::maximum(r, e, name);
}

h::Expr minimum0(h::Expr e, const std::string name) {
    return h::minimum(e, name);
}

h::Expr minimum1(h::RDom r, h::Expr e, const std::string name) {
    return h::minimum(r, e, name);
}

p::object argmin0(h::Expr e, const std::string name) {
    return expr_vector_to_python_tuple(h::argmin(e, name).as_vector());
}

p::object argmin1(h::RDom r, h::Expr e, const std::string name) {
    return expr_vector_to_python_tuple(h::argmin(r, e, name).as_vector());
}

p::object argmax0(h::Expr e, const std::string name) {
    return expr_vector_to_python_tuple(h::argmin(e, name).as_vector());
}

p::object argmax1(h::RDom r, h::Expr e, const std::string name) {
    return expr_vector_to_python_tuple(h::argmax(r, e, name).as_vector());
}

void defineInlineReductions() {
    // Defines some inline reductions: sum, product, minimum, maximum.

    p::def("sum", &sum0, (p::arg("e"), p::arg("name") = "sum"),
           "An inline reduction.");
    p::def("sum", &sum1, (p::arg("r"), p::arg("e"), p::arg("name") = "sum"),
           "An inline reduction.");

    p::def("product", &product0, (p::arg("e"), p::arg("name") = "product"),
           "An inline reduction.");
    p::def("product", &product1, (p::arg("r"), p::arg("e"), p::arg("name") = "product"),
           "An inline reduction.");

    p::def("maximum", &maximum0, (p::arg("e"), p::arg("name") = "maximum"),
           "An inline reduction.");
    p::def("maximum", &maximum1, (p::arg("r"), p::arg("e"), p::arg("name") = "maximum"),
           "An inline reduction.");

    p::def("minimum", &minimum0, (p::arg("e"), p::arg("name") = "minimum"),
           "An inline reduction.");
    p::def("minimum", &minimum1, (p::arg("r"), p::arg("e"), p::arg("name") = "minimum"),
           "An inline reduction.");

    p::def("argmin", &argmin0, (p::arg("e"), p::arg("name") = "argmin"),
           "An inline reduction.");
    p::def("argmin", &argmin1, (p::arg("r"), p::arg("e"), p::arg("name") = "argmin"),
           "An inline reduction.");

    p::def("argmax", &argmax0, (p::arg("e"), p::arg("name") = "argmax"),
           "An inline reduction.");
    p::def("argmax", &argmax1, (p::arg("r"), p::arg("e"), p::arg("name") = "argmax"),
           "An inline reduction.");

    return;
}
