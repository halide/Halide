#include "PyExpr.h"

#include <iomanip>

#include "PyBinaryOperators.h"
#include "PyType.h"

namespace Halide {
namespace PythonBindings {

void define_expr(py::module_ &m) {
    auto to_bool = [](const Expr &e) -> bool {
        std::ostringstream o;
        o << e;
        throw py::value_error(("The halide.Expr (" +
                               o.str() +
                               ") cannot be converted to a bool. "
                               "If this error occurs using the 'and'/'or' keywords, "
                               "consider using the '&'/'|' operators instead.")
                                  .c_str());
    };

    auto expr_class =
        py::class_<Expr>(m, "Expr")
            .def(py::init<>())
            .def("__init__", [](Expr *self, bool b) {
                new (self) Expr(Internal::make_bool(b));
            })
            // PyBind11 searches in declared order,
            // int should be tried before float conversion
            .def(py::init<int>())
            .def(py::init<int64_t>())
            // Python float is implemented by double
            // But Halide prohibits implicitly construct by double.
            .def("__init__", [](Expr *self, double v) {
                new (self) Expr(double_to_expr_check(v));
            })
            .def(py::init<std::string>())

            // for implicitly_convertible
            .def("__init__", [](Expr *self, const FuncRef &f) { new (self) Expr(f); })
            .def("__init__", [](Expr *self, const FuncTupleElementRef &f) { new (self) Expr(f); })
            .def("__init__", [](Expr *self, const FieldRef &f) { new (self) Expr(f); })
            .def("__init__", [](Expr *self, const Param<> &p) { new (self) Expr(p); })
            .def("__init__", [](Expr *self, const RDom &r) { new (self) Expr(r); })
            .def("__init__", [](Expr *self, const RVar &r) { new (self) Expr(r); })
            .def("__init__", [](Expr *self, const Var &v) { new (self) Expr(v); })

            .def("__bool__", to_bool)
            .def("__nonzero__", to_bool)

            .def("type", &Expr::type)
            .def("defined", &Expr::defined)
            .def("__repr__", [](const Expr &e) -> std::string {
                std::ostringstream o;
                if (e.defined()) {
                    o << "<halide.Expr of type " << halide_type_to_string(e.type()) << ": " << e << ">";
                } else {
                    o << "<undefined halide.Expr>";
                }
                return o.str();
            })
            .def("__str__", [](const Expr &e) -> std::string {
                std::ostringstream o;
                o << e;
                return o.str();
            });

    add_binary_operators(expr_class);

    // implicitly_convertible declaration order matters,
    // int should be tried before float conversion
    py::implicitly_convertible<bool, Expr>();
    py::implicitly_convertible<int, Expr>();
    py::implicitly_convertible<int64_t, Expr>();
    py::implicitly_convertible<float, Expr>();
    py::implicitly_convertible<double, Expr>();

    // There must be an Expr() ctor available for each of these
    py::implicitly_convertible<FuncRef, Expr>();
    py::implicitly_convertible<FuncTupleElementRef, Expr>();
    py::implicitly_convertible<FieldRef, Expr>();
    py::implicitly_convertible<Param<>, Expr>();
    py::implicitly_convertible<RDom, Expr>();
    py::implicitly_convertible<RVar, Expr>();
    py::implicitly_convertible<Var, Expr>();

    auto eviction_key_class =
        py::class_<EvictionKey>(m, "EvictionKey")
            .def(py::init<Expr>());

    py::implicitly_convertible<Expr, EvictionKey>();

    auto range_class =
        py::class_<Range>(m, "Range")
            .def(py::init<>())
            .def("__init__", [](Range *self, const Expr &min, const Expr &extent) {
                new (self) Range(Range(min, extent));
            })
            // Allow implicit conversion from py::tuple -> Range, iff py::tuple.size() == 2
            .def("__init__", [](Range *self, const py::tuple &t) {
                if (t.size() != 2) {
                    throw py::value_error("Halide::Range requires exactly two values");
                }
                Expr min = py::cast<Expr>(t[0]);
                Expr extent = py::cast<Expr>(t[1]);
                new (self) Range(Range(min, extent));
            })
            .def_rw("min", &Range::min)
            .def_rw("extent", &Range::extent);

    // Allow implicit tuple->Range conversion, so that things like
    // [(0, W), (0, H)] work for BoundaryConditions and Estimates
    py::implicitly_convertible<py::tuple, Range>();
}

}  // namespace PythonBindings
}  // namespace Halide
