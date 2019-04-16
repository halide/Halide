#include "PyDerivative.h"

namespace Halide {
namespace PythonBindings {

void define_derivative(py::module &m) {
    auto derivative_class = py::class_<Derivative>(m, "Derivative")
        .def("__getitem__", [](const Derivative &d, const Func &func) {return d(func);})
        .def("__getitem__", [](const Derivative &d, const Func &func, int update_id) {return d(func, update_id);})
        .def("__getitem__", [](const Derivative &d, const Buffer<> &buffer) {return d(buffer);});

    m.def("propagate_adjoints",
        (Derivative (*)(const Func &, const Func &, const std::vector<std::pair<Expr, Expr>> &))&propagate_adjoints);
    m.def("propagate_adjoints",
        (Derivative (*)(const Func &, const Buffer<float> &))&propagate_adjoints);
    m.def("propagate_adjoints",
        (Derivative (*)(const Func &))&propagate_adjoints);
}

}  // namespace PythonBindings
}  // namespace Halide