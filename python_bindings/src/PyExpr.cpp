#include "PyExpr.h"

#include <iomanip>

#include "PyBinaryOperators.h"
#include "PyType.h"

namespace Halide {
namespace PythonBindings {

void define_expr(py::module &m) {
    auto to_bool = [](const Expr &e) -> bool {
        std::ostringstream o;
        o << e;
        throw py::value_error("The halide.Expr (" + o.str() + ") cannot be converted to a bool. "
                                                              "If this error occurs using the 'and'/'or' keywords, consider using the '&'/'|' operators instead.");
        return false;
    };

    auto expr_class =
        py::class_<Expr>(m, "Expr")
            .def(py::init<>())
            // PyBind11 searches in declared order,
            // int should be tried before float conversion
            .def(py::init<int>())
            // Python float is implemented by double
            // But Halide prohibits implicitly construct by double.
            .def(py::init([](double v) {
                float f = static_cast<float>(v);
                double check = static_cast<double>(f);
                // 2^(-n) (or some combination) case is safe. e.g. 0.5, 0.25, 0.75, ...
                // otherwise, precision will be lost.  e.g. 0.1, 0.3, ...
                using Internal::reinterpret_bits;
                if (reinterpret_bits<uint64_t>(v) != reinterpret_bits<uint64_t>(check)) {
                    std::ostringstream oss;
                    oss.precision(17);
                    oss << std::fixed << v;
                    PyErr_WarnEx(
                        PyExc_RuntimeWarning,
                        ("The floating-point value " + oss.str() + " will be interpreted as a float32 by Halide and lose precision;"
                                                                   " add an explicit `f32()` or `f64()`` cast to avoid this warning.")
                            .c_str(),
                        0);
                }
                return Expr(f);
            }))
            .def(py::init<std::string>())

            // for implicitly_convertible
            .def(py::init([](const FuncRef &f) -> Expr { return f; }))
            .def(py::init([](const FuncTupleElementRef &f) -> Expr { return f; }))
            .def(py::init([](const Param<> &p) -> Expr { return p; }))
            .def(py::init([](const RDom &r) -> Expr { return r; }))
            .def(py::init([](const RVar &r) -> Expr { return r; }))
            .def(py::init([](const Var &v) -> Expr { return v; }))

            .def("__bool__", to_bool)
            .def("__nonzero__", to_bool)

            .def("type", &Expr::type)
            .def("__repr__", [](const Expr &e) -> std::string {
                std::ostringstream o;
                o << "<halide.Expr of type " << halide_type_to_string(e.type()) << ">";
                return o.str();
            });

    add_binary_operators(expr_class);

    // implicitly_convertible declaration order matters,
    // int should be tried before float conversion
    py::implicitly_convertible<int, Expr>();
    py::implicitly_convertible<float, Expr>();
    py::implicitly_convertible<double, Expr>();

    // There must be an Expr() ctor available for each of these
    py::implicitly_convertible<FuncRef, Expr>();
    py::implicitly_convertible<FuncTupleElementRef, Expr>();
    py::implicitly_convertible<Param<>, Expr>();
    py::implicitly_convertible<RDom, Expr>();
    py::implicitly_convertible<RVar, Expr>();
    py::implicitly_convertible<Var, Expr>();

    auto range_class =
        py::class_<Range>(m, "Range")
            .def(py::init<>())
            .def(py::init([](const Expr &min, const Expr &extent) -> Range {
                return Range(min, extent);
            }))
            // Allow implicit conversion from py::tuple -> Range, iff py::tuple.size() == 2
            .def(py::init([](const py::tuple &t) -> Range {
                if (t.size() != 2) {
                    throw py::value_error("Halide::Range requires exactly two values");
                }
                Expr min = t[0].cast<Expr>();
                Expr extent = t[1].cast<Expr>();
                return Range(min, extent);
            }))
            .def_readwrite("min", &Range::min)
            .def_readwrite("extent", &Range::extent);

    // Allow implicit tuple->Range conversion, so that things like
    // [(0, W), (0, H)] work for BoundaryConditions and Estimates
    py::implicitly_convertible<py::tuple, Range>();
}

}  // namespace PythonBindings
}  // namespace Halide
