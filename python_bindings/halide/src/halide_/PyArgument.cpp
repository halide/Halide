#include "PyArgument.h"

namespace Halide {
namespace PythonBindings {

void define_argument(py::module_ &m) {
    auto argument_estimates_class =
        py::class_<ArgumentEstimates>(m, "ArgumentEstimates")
            .def(py::init<>())
            .def_rw("scalar_def", &ArgumentEstimates::scalar_def)
            .def_rw("scalar_min", &ArgumentEstimates::scalar_min)
            .def_rw("scalar_max", &ArgumentEstimates::scalar_max)
            .def_rw("scalar_estimate", &ArgumentEstimates::scalar_estimate)
            .def_rw("buffer_estimates", &ArgumentEstimates::buffer_estimates);

    auto argument_class =
        py::class_<Argument>(m, "Argument")
            .def(py::init<>())
            .def("__init__", [](Argument *self, const OutputImageParam &im) { new (self) Argument(im); }, py::arg("im"))
            .def("__init__", [](Argument *self, const ImageParam &im) { new (self) Argument(im); }, py::arg("im"))
            .def("__init__", [](Argument *self, const Param<> &param) { new (self) Argument(param); }, py::arg("param"))
            .def(py::init<Buffer<>>(), py::arg("buffer"))
            .def_rw("name", &Argument::name)
            .def_rw("kind", &Argument::kind)
            .def_rw("dimensions", &Argument::dimensions)
            .def_rw("type", &Argument::type)
            .def_rw("argument_estimates", &Argument::argument_estimates);

    py::implicitly_convertible<Buffer<>, Argument>();
    py::implicitly_convertible<ImageParam, Argument>();
    py::implicitly_convertible<OutputImageParam, Argument>();
    py::implicitly_convertible<Param<>, Argument>();
}

}  // namespace PythonBindings
}  // namespace Halide
