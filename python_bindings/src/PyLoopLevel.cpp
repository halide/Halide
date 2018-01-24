#include "PyLoopLevel.h"

namespace Halide {
namespace PythonBindings {

void define_loop_level(py::module &m) {
    // Note that the public-but-only-intended-for-internal-use methods
    // are deliberately omitted.
    auto looplevel_class = py::class_<LoopLevel>(m, "LoopLevel")
        .def(py::init<>())
        .def(py::init<const Func &, VarOrRVar, int>(),
             py::arg("func"), py::arg("var"), py::arg("stage_index") = -1)
        .def("stage_index", &LoopLevel::stage_index)
        .def("set", &LoopLevel::set)
        .def_static("inlined", &LoopLevel::inlined)
        .def_static("root", &LoopLevel::root)
    ;
}

}  // namespace PythonBindings
}  // namespace Halide
