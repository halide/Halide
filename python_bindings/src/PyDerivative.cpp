#include "PyDerivative.h"

namespace Halide {
namespace PythonBindings {

void define_derivative(py::module &m) {
    auto derivative_class = py::class_<Derivative>(m, "Derivative")
        .def("__getitem__", [](const Derivative &d, const Func &func) {
            return d(func);
        }, py::arg("func"))
        .def("__getitem__", [](const Derivative &d, const Buffer<> &buffer) {
            return d(buffer);
        }, py::arg("buffer"))
        .def("__getitem__", [](const Derivative &d, const Param<> &param) {
            return d(param);
        }, py::arg("param"))
        .def("__getitem__", [](const Derivative &d, const std::tuple<const Func &, int> &args) {
            return d(std::get<0>(args), std::get<1>(args));
        });

    m.def("propagate_adjoints",
        (Derivative (*)(const Func &, const Func &, const Region &))&propagate_adjoints);
    m.def("propagate_adjoints",
        (Derivative (*)(const Func &, const Buffer<float> &))&propagate_adjoints);
    m.def("propagate_adjoints",
        (Derivative (*)(const Func &))&propagate_adjoints);
}

}  // namespace PythonBindings
}  // namespace Halide