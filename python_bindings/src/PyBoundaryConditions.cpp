#include "PyBoundaryConditions.h"

namespace Halide {
namespace PythonBindings {

namespace {

inline Func to_func(const Buffer<> &b) {
    return lambda(_, b(_));
}

}  // namespace

void define_boundary_conditions(py::module &m) {
    using namespace BoundaryConditions;

    py::module bc = m.def_submodule("BoundaryConditions");

    // This code could be made less redundant with some templating,
    // but because of the templated nature of BoundaryConditions itself,
    // we'd need at least a couple of template levels; not clear it would
    // be a win in either code size or readability.

    // ----- constant_exterior

    bc.def("constant_exterior", [](ImageParam im, Expr exterior) -> Func {
        return constant_exterior(im, exterior);
    }, py::arg("f"), py::arg("exterior"));
    bc.def("constant_exterior", [](Buffer<> b, Expr exterior) -> Func {
        return constant_exterior(b, exterior);
    }, py::arg("f"), py::arg("exterior"));
    bc.def("constant_exterior", [](py::object target, Expr exterior,
                                   Region bounds) -> Func {
        try {
            return constant_exterior(target.cast<Func>(), exterior, bounds);
        } catch (...) {
            // fall thru
        }
        try {
            return constant_exterior(to_func(target.cast<Buffer<>>()), exterior, bounds);
        } catch (...) {
            // fall thru
        }
        throw py::value_error("Invalid arguments to constant_exterior");
        return Func();
    }, py::arg("f"), py::arg("exterior"), py::arg("bounds"));

    // ----- repeat_edge
    bc.def("repeat_edge", [](ImageParam im) -> Func {
        return repeat_edge(im);
    }, py::arg("f"));
    bc.def("repeat_edge", [](Buffer<> b) -> Func {
        return repeat_edge(b);
    }, py::arg("f"));
    bc.def("repeat_edge", [](py::object target, Region bounds) -> Func {
        try {
            return repeat_edge(target.cast<Func>(), bounds);
        } catch (...) {
            // fall thru
        }
        try {
            return repeat_edge(to_func(target.cast<Buffer<>>()), bounds);
        } catch (...) {
            // fall thru
        }
        throw py::value_error("Invalid arguments to repeat_edge");
        return Func();
    }, py::arg("f"), py::arg("bounds"));

    // ----- repeat_image
    bc.def("repeat_image", [](ImageParam im) -> Func {
        return repeat_image(im);
    }, py::arg("f"));
    bc.def("repeat_image", [](Buffer<> b) -> Func {
        return repeat_image(b);
    }, py::arg("f"));
    bc.def("repeat_image", [](py::object target, Region bounds) -> Func {
        try {
            return repeat_image(target.cast<Func>(), bounds);
        } catch (...) {
            // fall thru
        }
        try {
            return repeat_image(to_func(target.cast<Buffer<>>()), bounds);
        } catch (...) {
            // fall thru
        }
        throw py::value_error("Invalid arguments to repeat_image");
        return Func();
    }, py::arg("f"), py::arg("bounds"));

    // ----- mirror_image
    bc.def("mirror_image", [](ImageParam im) -> Func {
        return mirror_image(im);
    }, py::arg("f"));
    bc.def("mirror_image", [](Buffer<> b) -> Func {
        return mirror_image(b);
    }, py::arg("f"));
    bc.def("mirror_image", [](py::object target, Region bounds) -> Func {
        try {
            return mirror_image(target.cast<Func>(), bounds);
        } catch (...) {
            // fall thru
        }
        try {
            return mirror_image(to_func(target.cast<Buffer<>>()), bounds);
        } catch (...) {
            // fall thru
        }
        throw py::value_error("Invalid arguments to mirror_image");
        return Func();
    }, py::arg("f"), py::arg("bounds"));

    // ----- mirror_interior
    bc.def("mirror_interior", [](ImageParam im) -> Func {
        return mirror_interior(im);
    }, py::arg("f"));
    bc.def("mirror_interior", [](Buffer<> b) -> Func {
        return mirror_interior(b);
    }, py::arg("f"));
    bc.def("mirror_interior", [](py::object target, Region bounds) -> Func {
        try {
            return mirror_interior(target.cast<Func>(), bounds);
        } catch (...) {
            // fall thru
        }
        try {
            return mirror_interior(to_func(target.cast<Buffer<>>()), bounds);
        } catch (...) {
            // fall thru
        }
        throw py::value_error("Invalid arguments to mirror_interior");
        return Func();
    }, py::arg("f"), py::arg("bounds"));

}

}  // namespace PythonBindings
}  // namespace Halide
