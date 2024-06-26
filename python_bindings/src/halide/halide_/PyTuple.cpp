#include "PyTuple.h"

namespace Halide {
namespace PythonBindings {

Tuple to_halide_tuple(const py::object &o) {
    try {
        Expr e = o.cast<Expr>();
        return Tuple(e);
    } catch (...) {
        // fall thru
    }

    try {
        py::tuple t = o.cast<py::tuple>();
        if (t.empty()) {
            throw py::value_error("Cannot use a zero-length tuple-of-Expr");
        }
        std::vector<Expr> v(t.size());
        for (size_t i = 0; i < t.size(); i++) {
            v[i] = t[i].cast<Expr>();
        }
        return Tuple(v);
    } catch (...) {
        // fall thru
    }

    throw py::value_error("Expected an Expr or tuple-of-Expr.");
}

void define_tuple(py::module &m) {
    // Halide::Tuple isn't surfaced to the user in Python;
    // we define it here to allow PyBind to do some automatic
    // conversion from Python's built-in tuple type.
    //
    // TODO: AFAICT, there isn't a good way to auto-convert Halide::Tuple-as-return-type
    // *TO* a py::tuple; call sites that return Halide::Tuple currently must manually
    // do this (typically via a C++ lambda that calls to_python_tuple()).
    auto tuple_class =
        py::class_<Tuple>(m, "Tuple")
            // for implicitly_convertible
            .def(py::init([](const py::tuple &t) -> Tuple {
                std::vector<Expr> v;
                v.reserve(t.size());
                for (const auto o : t) {
                    v.push_back(o.cast<Expr>());
                }
                return Tuple(v);
            }))
            .def(py::init([](const FuncRef &f) -> Tuple {
                std::vector<Expr> v;
                v.reserve(f.size());
                if (f.size() == 1) {
                    v.push_back(f);
                } else {
                    for (size_t i = 0; i < f.size(); ++i) {
                        v.push_back(f[(int)i]);
                    }
                }
                return Tuple(v);
            }))
            .def(py::init([](const std::vector<Expr> &v) -> Tuple {
                return Tuple(v);
            }))
            .def("__repr__", [](const Tuple &t) -> std::string {
                std::ostringstream o;
                o << "<halide.Tuple of size " << t.size() << ">";
                return o.str();
            });

    py::implicitly_convertible<py::tuple, Tuple>();

    // If we autoconvert from vector<Expr>, we must also special-case FuncRef, alas
    py::implicitly_convertible<FuncRef, Tuple>();

    // We want to allow vector<Expr>->Tuple implicitly, so that
    // custom classes that are tuple-like will autoconvert (see tutorial/lesson_13).
    py::implicitly_convertible<std::vector<Expr>, Tuple>();
}

}  // namespace PythonBindings
}  // namespace Halide
