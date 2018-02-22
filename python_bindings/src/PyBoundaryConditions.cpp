#include "PyBoundaryConditions.h"

namespace Halide {
namespace PythonBindings {

namespace {

template<typename FuncLike>
Func try_repeat_edge(py::object o, const std::vector<std::pair<Expr, Expr>> &bounds) {
    FuncLike f = o.template cast<FuncLike>();
    return BoundaryConditions::repeat_edge(BoundaryConditions::Internal::func_like_to_func(f), bounds);
}

}  // namespace

void define_boundary_conditions(py::module &m) {
    // ImageParam and Buffer can be called with no extra args.
    m.def("repeat_edge", [](ImageParam im) -> Func {
        return BoundaryConditions::repeat_edge(im);
    });
    m.def("repeat_edge", [](Buffer<> b) -> Func {
        return BoundaryConditions::repeat_edge(b);
    });
    // The extra-arg form can accept Func, ImageParam, or Buffer.
    m.def("repeat_edge", [](py::args args) -> Func {
        if (args.size() > 1 && (args.size() % 2) == 0) {
            throw py::value_error("Invalid arguments to repeat_edge");
        }
        std::vector<std::pair<Expr, Expr>> bounds;
        for (size_t i = 1; i < args.size(); i += 2) {
            bounds.push_back({args[i].cast<Expr>(), args[i+1].cast<Expr>()});
        }
        // args[0] can be Func, ImageParam, or Buffer<>.
        // No way to see if a cast will work: just have to try and fail.
        // TODO: surely there's a better way.
        try {
            return try_repeat_edge<Func>(args[0], bounds);
        } catch (...) {
            // fall thru
        }
        try {
            return try_repeat_edge<ImageParam>(args[0], bounds);
        } catch (...) {
            // fall thru
        }
        try {
            return try_repeat_edge<Buffer<>>(args[0], bounds);
        } catch (...) {
            // fall thru
        }
        throw py::value_error("Invalid arguments to repeat_edge");
        return Func();
    });
}

}  // namespace PythonBindings
}  // namespace Halide
