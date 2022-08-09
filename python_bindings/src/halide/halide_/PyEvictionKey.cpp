#include "PyEvictionKey.h"

namespace Halide {
namespace PythonBindings {

namespace {

std::string ek_repr(const EvictionKey &t) {
    std::ostringstream o;
    o << "<halide.EvictionKey >";
    return o.str();
}

}  // namespace

void define_evictionkey(py::module &m) {

    auto evictionkey_class =
        py::class_<EvictionKey>(m, "EvictionKey")
            .def(py::init<>())
            .def(py::init<const Expr &>())
            // .def("__repr__", &ek_repr)
            // .def("__str__", &ek_repr) //&Target::to_string)
    ;

}

}  // namespace PythonBindings
}  // namespace Halide

// ---- reference ----
// class EvictionKey {
// protected:
//     Expr key;
//     friend class Func;
// public:
//     explicit EvictionKey(const Expr &expr = Expr())
//         : key(expr) {
//     }
// };
