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
        .def(py::init([](py::args args) -> RDom {
            std::string name;
            size_t end_offset = 0;
            if (args.size() % 2) {
                // If number of args is odd, last arg must be a string.
                name = args[args.size()-1].cast<std::string>();
                end_offset = 1;
            }
            return RDom(args_to_pair_vector<Expr,Expr>(args, 0, end_offset), name);
        }))
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
