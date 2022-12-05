#include "PyVar.h"

#include "PyBinaryOperators.h"

namespace Halide {
namespace PythonBindings {

namespace {

std::string var_repr(const Var &var) {
    std::ostringstream o;
    o << "<halide.Var '" << var.name() << "'>";
    return o.str();
}

}  // namespace

void define_var(py::module &m) {
    auto var_class =
        py::class_<Var>(m, "Var")
            .def(py::init<>())
            .def(py::init<std::string>())
            .def("name", &Var::name)
            .def("same_as", &Var::same_as)
            .def("is_implicit", (bool(Var::*)() const) & Var::is_implicit)
            .def("implicit_index", (int(Var::*)() const) & Var::implicit_index)
            .def("is_placeholder", (bool(Var::*)() const) & Var::is_placeholder)
            .def_static("implicit", (Var(*)(int)) & Var::implicit)
            .def_static("outermost", &Var::outermost)
            .def("__repr__", &var_repr)
            .def("__str__", &Var::name);

    add_binary_operators(var_class);

    m.attr("_") = Halide::Var(Halide::_);
    m.attr("_0") = Halide::Var(Halide::_0);
    m.attr("_1") = Halide::Var(Halide::_1);
    m.attr("_2") = Halide::Var(Halide::_2);
    m.attr("_3") = Halide::Var(Halide::_3);
    m.attr("_4") = Halide::Var(Halide::_4);
    m.attr("_5") = Halide::Var(Halide::_5);
    m.attr("_6") = Halide::Var(Halide::_6);
    m.attr("_7") = Halide::Var(Halide::_7);
    m.attr("_8") = Halide::Var(Halide::_8);
    m.attr("_9") = Halide::Var(Halide::_9);
}

}  // namespace PythonBindings
}  // namespace Halide
