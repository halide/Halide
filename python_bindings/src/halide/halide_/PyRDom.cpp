#include "PyRDom.h"

#include "PyBinaryOperators.h"

namespace Halide {
namespace PythonBindings {

namespace {
void define_rvar(py::module &m) {
    auto rvar_class =
        py::class_<RVar>(m, "RVar")
            .def(py::init<>())
            .def(py::init<std::string>(), py::arg("name"))
            .def(py::init([](const RDom &r) -> RVar { return r; }))
            .def("min", &RVar::min)
            .def("extent", &RVar::extent)
            .def("name", &RVar::name)
            .def("__repr__", [](const RVar &v) -> std::string {
                std::ostringstream o;
                o << "<halide.RVar " << v << ">";
                return o.str();
            });

    py::implicitly_convertible<RDom, RVar>();

    add_binary_operators(rvar_class);
}
}  // namespace

void define_rdom(py::module &m) {
    define_rvar(m);

    // A small iterator wrapper to expose RDom iteration to Python.
    // It holds a copy of the RDom and an index, and implements
    // the iterator protocol (__iter__ and __next__).
    struct RDomIterator {
        RDom rd;
        int idx = 0;
        RDomIterator() = default;
        RDomIterator(const RDom &r) : rd(r) {
        }
        RVar next() {
            if (idx >= rd.dimensions()) {
                throw py::stop_iteration();
            }
            return rd[idx++];
        }
        RDomIterator &iter() {
            return *this;
        }
    };

    // Expose the iterator type to Python so we can return it from __iter__.
    py::class_<RDomIterator>(m, "_RDomIterator")
        .def(py::init<>())
        .def(py::init<const RDom &>())
        .def("__iter__", &RDomIterator::iter)
        .def("__next__", &RDomIterator::next);

    auto rdom_class =
        py::class_<RDom>(m, "RDom")
            .def(py::init<>())
            .def(py::init<Buffer<>>(), py::arg("buffer"))
            .def(py::init<OutputImageParam>(), py::arg("image_param"))
            .def(py::init<const Region &, std::string>(), py::arg("region"), py::arg("name") = "")
            .def("domain", &RDom::domain)
            .def("defined", &RDom::defined)
            .def("same_as", &RDom::same_as)
            .def("dimensions", &RDom::dimensions)
            .def("__len__", &RDom::dimensions)
            .def("__iter__", [](const RDom &r) { return RDomIterator(r); }, py::keep_alive<0, 1>())
            .def("where", &RDom::where, py::arg("predicate"))
            .def("__getitem__", [](RDom &r, const int i) -> RVar {
                if (i < 0 || i >= r.dimensions()) {
                    throw pybind11::key_error();
                }
                return r[i];  //
            })
            .def_readonly("x", &RDom::x)
            .def_readonly("y", &RDom::y)
            .def_readonly("z", &RDom::z)
            .def_readonly("w", &RDom::w)
            .def("__repr__", [](const RDom &r) -> std::string {
                std::ostringstream o;
                o << "<halide.RDom " << r << ">";
                return o.str();  //
            });

    add_binary_operators(rdom_class);
}

}  // namespace PythonBindings
}  // namespace Halide
