#include "PyImageParam.h"

#include "PyType.h"

namespace Halide {
namespace PythonBindings {

void define_image_param(py::module &m) {
    auto image_param_class =
        py::class_<ImageParam>(m, "ImageParam")
        .def(py::init<>())
        .def(py::init<Type, int>())
        .def(py::init<Type, int, std::string>())
        .def("name", &ImageParam::name)
        .def("set", &ImageParam::set)
        .def("get", &ImageParam::get)
        .def("reset", &ImageParam::reset)
        .def("__getitem__", [](ImageParam &im, const Expr &args) -> Expr {
            return im(args);
        })
        .def("__getitem__", [](ImageParam &im, const std::vector<Expr> &args) -> Expr {
            return im(args);
        })
        .def("width", &ImageParam::width)
        .def("height", &ImageParam::height)
        .def("dimensions", &ImageParam::dimensions)
        .def("left", &ImageParam::left)
        .def("right", &ImageParam::right)
        .def("top", &ImageParam::top)
        .def("bottom", &ImageParam::bottom)

        .def("__repr__", [](const ImageParam &im) -> std::string {
            std::ostringstream o;
            o << "<halide.ImageParam '" <<im.name() << "'";
            if (!im.defined()) {
                o << " (undefined)";
            } else {
                // TODO: add dimensions to this
                o << " type " << halide_type_to_string(im.type());
            }
            o << ">";
            return o.str();
        })
    ;
}



}  // namespace PythonBindings
}  // namespace Halide
