#include "PyArgument.h"

namespace Halide {
namespace PythonBindings {

void define_argument(py::module &m) {
    auto argument_estimates_class =
        py::class_<ArgumentEstimates>(m, "ArgumentEstimates")
            .def(py::init<>())
            .def_readwrite("scalar_def", &ArgumentEstimates::scalar_def)
            .def_readwrite("scalar_min", &ArgumentEstimates::scalar_min)
            .def_readwrite("scalar_max", &ArgumentEstimates::scalar_max)
            .def_readwrite("scalar_estimate", &ArgumentEstimates::scalar_estimate)
            .def_readwrite("buffer_estimates", &ArgumentEstimates::buffer_estimates);

    auto argument_class =
        py::class_<Argument>(m, "Argument")
            .def(py::init<>())
            .def(py::init([](const OutputImageParam &im) -> Argument {
                     return im;
                 }),
                 py::arg("im"))
            .def(py::init([](const ImageParam &im) -> Argument {
                     return im;
                 }),
                 py::arg("im"))
            .def(py::init([](const Param<> &param) -> Argument {
                     return param;
                 }),
                 py::arg("param"))
            .def(py::init<Buffer<>>(), py::arg("buffer"))
            .def_readwrite("name", &Argument::name)
            .def_readwrite("kind", &Argument::kind)
            .def_readwrite("dimensions", &Argument::dimensions)
            .def_readwrite("type", &Argument::type)
            .def_readwrite("argument_estimates", &Argument::argument_estimates);

    py::implicitly_convertible<Buffer<>, Argument>();
    py::implicitly_convertible<ImageParam, Argument>();
    py::implicitly_convertible<OutputImageParam, Argument>();
    py::implicitly_convertible<Param<>, Argument>();
}

}  // namespace PythonBindings
}  // namespace Halide
