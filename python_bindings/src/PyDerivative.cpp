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
        .def("__getitem__", [](const Derivative &d, const std::tuple<const Func &, int> &args) {
            return d(std::get<0>(args), std::get<1>(args), true);
        })
        .def("__getitem__", [](const Derivative &d, const std::tuple<const Func &, int, bool> &args) {
            return d(std::get<0>(args), std::get<1>(args), std::get<2>(args));
        })
        .def("get", [](const Derivative &d, const Func &func, int update_id, bool bounded) {
            return d.get(func, update_id, bounded);  
        }, py::arg("func"), py::arg("update_id") = -1, py::arg("bounded") = true)
        .def("get", [](const Derivative &d, const Buffer<> &buffer) {
            return d.get(buffer);
        }, py::arg("buffer"));

    m.def("propagate_adjoints",
        (Derivative (*)(const Func &, const Func &,
            const std::vector<std::pair<Expr, Expr>> &))&propagate_adjoints);
    m.def("propagate_adjoints",
        (Derivative (*)(const Func &, const Buffer<float> &))&propagate_adjoints);
    m.def("propagate_adjoints",
        (Derivative (*)(const Func &))&propagate_adjoints);
}

}  // namespace PythonBindings
}  // namespace Halide