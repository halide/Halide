#include "PyImageParam.h"

#include "PyType.h"

namespace Halide {
namespace PythonBindings {

void define_image_param(py::module &m) {
    using Dimension = Internal::Dimension;

    auto dimension_class =
        py::class_<Dimension>(m, "Dimension")
            .def("min", &Dimension::min)
            .def("stride", &Dimension::stride)
            .def("extent", &Dimension::extent)
            .def("max", &Dimension::max)
            .def("set_min", &Dimension::set_min, py::arg("min"))
            .def("set_extent", &Dimension::set_extent, py::arg("extent"))
            .def("set_stride", &Dimension::set_stride, py::arg("stride"))
            .def("set_bounds", &Dimension::set_bounds, py::arg("min"), py::arg("extent"))
            .def("set_estimate", &Dimension::set_estimate, py::arg("min"), py::arg("extent"))
            .def("dim", (Dimension(Dimension::*)(int)) & Dimension::dim, py::arg("dimension"), py::keep_alive<0, 1>());

    auto output_image_param_class =
        py::class_<OutputImageParam>(m, "OutputImageParam")
            .def(py::init<>())
            .def("name", &OutputImageParam::name)
            .def("type", &OutputImageParam::type)
            .def("defined", &OutputImageParam::defined)
            .def("dim", (Dimension(OutputImageParam::*)(int)) & OutputImageParam::dim, py::arg("dimension"), py::keep_alive<0, 1>())
            .def("host_alignment", &OutputImageParam::host_alignment)
            .def("set_estimates", &OutputImageParam::set_estimates, py::arg("estimates"))
            .def("set_host_alignment", &OutputImageParam::set_host_alignment)
            .def("store_in", &OutputImageParam::store_in, py::arg("memory_type"))
            .def("dimensions", &OutputImageParam::dimensions)
            .def("left", &OutputImageParam::left)
            .def("right", &OutputImageParam::right)
            .def("top", &OutputImageParam::top)
            .def("bottom", &OutputImageParam::bottom)
            .def("width", &OutputImageParam::width)
            .def("height", &OutputImageParam::height)
            .def("channels", &OutputImageParam::channels)

            .def("__repr__", [](const OutputImageParam &im) -> std::string {
                std::ostringstream o;
                o << "<halide.OutputImageParam '" << im.name() << "'";
                if (!im.defined()) {
                    o << " (undefined)";
                } else {
                    // TODO: add dimensions to this
                    o << " type " << halide_type_to_string(im.type());
                }
                o << ">";
                return o.str();
            });

    auto image_param_class =
        py::class_<ImageParam>(m, "ImageParam", output_image_param_class)
            .def(py::init<>())
            .def(py::init<Type, int>(), py::arg("type"), py::arg("dimensions"))
            .def(py::init<Type, int, std::string>(), py::arg("type"), py::arg("dimensions"), py::arg("name"))
            .def("set", &ImageParam::set)
            .def("get", &ImageParam::get)
            .def("reset", &ImageParam::reset)
            .def("__getitem__", [](ImageParam &im, const Expr &args) -> Expr {
                return im(args);
            })
            .def("__getitem__", [](ImageParam &im, const std::vector<Expr> &args) -> Expr {
                return im(args);
            })
            .def("__getitem__", [](ImageParam &im, const std::vector<Var> &args) -> Expr {
                return im(args);
            })
            .def("in", (Func(ImageParam::*)(const Func &)) & ImageParam::in)
            .def("in", (Func(ImageParam::*)(const std::vector<Func> &)) & ImageParam::in)
            .def("in", (Func(ImageParam::*)()) & ImageParam::in)
            .def("trace_loads", &ImageParam::trace_loads)

            .def("__repr__", [](const ImageParam &im) -> std::string {
                std::ostringstream o;
                o << "<halide.ImageParam '" << im.name() << "'";
                if (!im.defined()) {
                    o << " (undefined)";
                } else {
                    // TODO: add dimensions to this
                    o << " type " << halide_type_to_string(im.type());
                }
                o << ">";
                return o.str();
            });
}

}  // namespace PythonBindings
}  // namespace Halide
