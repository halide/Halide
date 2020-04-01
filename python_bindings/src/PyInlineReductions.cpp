#include "PyInlineReductions.h"

#include <initializer_list>
#include <string>
#include <utility>

#include "Halide.h"
#include "PyTuple.h"
#include "pybind11/cast.h"
#include "pybind11/detail/../pytypes.h"
#include "pybind11/detail/descr.h"
#include "pybind11/pybind11.h"

namespace Halide {
namespace PythonBindings {

void define_inline_reductions(py::module &m) {
    m.def("sum", (Expr(*)(Expr, const std::string &s)) & Halide::sum,
          py::arg("expr"), py::arg("name") = "sum");
    m.def("sum", (Expr(*)(const RDom &, Expr, const std::string &s)) & Halide::sum,
          py::arg("rdom"), py::arg("expr"), py::arg("name") = "sum");

    m.def("product", (Expr(*)(Expr, const std::string &s)) & Halide::product,
          py::arg("expr"), py::arg("name") = "product");
    m.def("product", (Expr(*)(const RDom &, Expr, const std::string &s)) & Halide::product,
          py::arg("rdom"), py::arg("expr"), py::arg("name") = "product");

    m.def("maximum", (Expr(*)(Expr, const std::string &s)) & Halide::maximum,
          py::arg("expr"), py::arg("name") = "maximum");
    m.def("maximum", (Expr(*)(const RDom &, Expr, const std::string &s)) & Halide::maximum,
          py::arg("rdom"), py::arg("expr"), py::arg("name") = "maximum");

    m.def("minimum", (Expr(*)(Expr, const std::string &s)) & Halide::minimum,
          py::arg("expr"), py::arg("name") = "minimum");
    m.def("minimum", (Expr(*)(const RDom &, Expr, const std::string &s)) & Halide::minimum,
          py::arg("rdom"), py::arg("expr"), py::arg("name") = "minimum");

    m.def(
        "argmax", [](Expr e, const std::string &s) -> py::tuple {
            return to_python_tuple(argmax(e, s));
        },
        py::arg("expr"), py::arg("name") = "argmax");
    m.def(
        "argmax", [](const RDom &r, Expr e, const std::string &s) -> py::tuple {
            return to_python_tuple(argmax(r, e, s));
        },
        py::arg("rdom"), py::arg("expr"), py::arg("name") = "argmax");

    m.def(
        "argmin", [](Expr e, const std::string &s) -> py::tuple {
            return to_python_tuple(argmin(e, s));
        },
        py::arg("expr"), py::arg("name") = "argmin");
    m.def(
        "argmin", [](const RDom &r, Expr e, const std::string &s) -> py::tuple {
            return to_python_tuple(argmin(r, e, s));
        },
        py::arg("rdom"), py::arg("expr"), py::arg("name") = "argmin");
}

}  // namespace PythonBindings
}  // namespace Halide
