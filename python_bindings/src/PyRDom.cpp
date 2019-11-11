#include "PyRDom.h"

#include "PyBinaryOperators.h"

namespace Halide {
namespace PythonBindings {

void define_rvar(py::module &m) {
    auto rvar_class = py::class_<RVar>(m, "RVar")
        .def(py::init<>())
        .def(py::init<std::string>(), py::arg("name"))
        .def(py::init([](const RDom &r) -> RVar { return r; }))
        .def("min", &RVar::min)
        .def("extent", &RVar::extent)
        .def("name", &RVar::name)
    ;

    py::implicitly_convertible<RDom, RVar>();

    add_binary_operators_with<Expr>(rvar_class);
}

void define_rdom(py::module &m) {
    define_rvar(m);

    auto rdom_class = py::class_<RDom>(m, "RDom")
        .def(py::init<>())
        .def(py::init<Buffer<>>(), py::arg("buffer"))
        .def(py::init<OutputImageParam>(), py::arg("image_param"))
        .def(py::init<const Region &, std::string>(), py::arg("region"), py::arg("name") = "")
        .def("domain", &RDom::domain)
        .def("defined", &RDom::defined)
        .def("same_as", &RDom::same_as)
        .def("dimensions", &RDom::dimensions)
        .def("where", &RDom::where, py::arg("predicate"))
        .def_readonly("x", &RDom::x)
        .def_readonly("y", &RDom::y)
        .def_readonly("z", &RDom::z)
        .def_readonly("w", &RDom::w);

    add_binary_operators_with<Expr>(rdom_class);
}

}  // namespace PythonBindings
}  // namespace Halide
