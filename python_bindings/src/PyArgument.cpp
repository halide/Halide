#include "PyArgument.h"

namespace Halide {
namespace PythonBindings {

void define_argument(py::module &m) {
    py::enum_<Argument::Kind>(m, "ArgumentKind")
        .value("InputScalar", Argument::Kind::InputScalar)
        .value("InputBuffer", Argument::Kind::InputBuffer)
        .value("OutputBuffer", Argument::Kind::OutputBuffer);

    auto argument_class =
        py::class_<Argument>(m, "Argument")
        .def(py::init<>())
        .def(py::init([](ImageParam im) -> Argument {
            return im;
        }), py::arg("im"))
        .def(py::init([](Param<> param) -> Argument {
            return param;
        }), py::arg("param"))
        .def(py::init<Buffer<>>(), py::arg("buffer"))
        // Various accessors elided, as it's unlikely they are needed from Python user code.
    ;

    py::implicitly_convertible<Buffer<>, Argument>();
    py::implicitly_convertible<ImageParam, Argument>();
    py::implicitly_convertible<Param<>, Argument>();
}

}  // namespace PythonBindings
}  // namespace Halide
