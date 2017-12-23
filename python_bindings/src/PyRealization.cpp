#include "PyRealization.h"

namespace Halide {
namespace PythonBindings {

void define_realization(py::module &m) {
    // TODO: incomplete
    auto realization_class = py::class_<Realization>(m, "Realization")
        .def(py::init<Buffer<> &>())
        .def(py::init<std::vector<Buffer<>> &>())
        .def("size", &Realization::size)
        .def("__len__", &Realization::size)
        .def("__getitem__", (Buffer<> &(Realization::*)(size_t))&Realization::operator[])
    ;

    py::implicitly_convertible<Buffer<>, Realization>();
    py::implicitly_convertible<std::vector<Buffer<>>, Realization>();
}

}  // namespace PythonBindings
}  // namespace Halide
