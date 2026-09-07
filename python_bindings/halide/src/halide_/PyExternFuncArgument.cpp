#include "PyExternFuncArgument.h"

namespace Halide {
namespace PythonBindings {

void define_extern_func_argument(py::module &m) {
    auto extern_func_argument_class =
        py::class_<ExternFuncArgument>(m, "ExternFuncArgument")
            .def(py::init<>())
            .def(py::init<Buffer<>>())
            .def(py::init<Expr>())
            .def(py::init<int>())
            .def(py::init<float>())
            // for implicitly_convertible
            .def(py::init([](const Func &f) -> ExternFuncArgument { return f; }))
            .def(py::init([](const Param<> &p) -> ExternFuncArgument { return p; }))
            .def(py::init([](const ImageParam &im) -> ExternFuncArgument { return im; }))
            .def(py::init([](const OutputImageParam &im) -> ExternFuncArgument { return im; }))

            .def("is_func", &ExternFuncArgument::is_func)
            .def("is_expr", &ExternFuncArgument::is_expr)
            .def("is_buffer", &ExternFuncArgument::is_buffer)
            .def("is_image_param", &ExternFuncArgument::is_image_param)
            .def("defined", &ExternFuncArgument::defined);

    py::implicitly_convertible<Expr, ExternFuncArgument>();
    py::implicitly_convertible<Func, ExternFuncArgument>();
    py::implicitly_convertible<Buffer<>, ExternFuncArgument>();
    py::implicitly_convertible<Param<>, ExternFuncArgument>();
    py::implicitly_convertible<ImageParam, ExternFuncArgument>();
    py::implicitly_convertible<OutputImageParam, ExternFuncArgument>();
}

}  // namespace PythonBindings
}  // namespace Halide
