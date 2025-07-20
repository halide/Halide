#include "PyFuncRef.h"

#include "PyBinaryOperators.h"
#include "PyTuple.h"

namespace Halide {
namespace PythonBindings {

Expr operator-(const UnevaluatedFuncRefExpr &expr) {
    return -Expr(expr);
}

Expr operator~(const UnevaluatedFuncRefExpr &expr) {
    return ~Expr(expr);
}

void define_func_ref(py::module &m) {
    auto undefined_lhs_funcref_expr_class =
        py::class_<UnevaluatedFuncRefExpr>(m, "_UndefinedLHSFuncRefExpr");
    add_binary_operators(undefined_lhs_funcref_expr_class);

    auto func_tuple_element_ref_class =
        py::class_<FuncTupleElementRef>(m, "FuncTupleElementRef")
            .def("index", &FuncTupleElementRef::index);
    add_binary_operators(func_tuple_element_ref_class);

    auto func_ref_class =
        py::class_<FuncRef>(m, "FuncRef")
            .def("__getitem__", &FuncRef::operator[])
            .def("size", &FuncRef::size)
            .def("__len__", &FuncRef::size)
            .def("__add__", [](const FuncRef &self, const Expr &other) {
                return UnevaluatedFuncRefExpr{self, other, UnevaluatedFuncRefExpr::Op::Add};
            })
            .def("__sub__", [](const FuncRef &self, const Expr &other) {
                return UnevaluatedFuncRefExpr{self, other, UnevaluatedFuncRefExpr::Op::Sub};
            })
            .def("__mul__", [](const FuncRef &self, const Expr &other) {
                return UnevaluatedFuncRefExpr{self, other, UnevaluatedFuncRefExpr::Op::Mul};
            })
            .def("__truediv__", [](const FuncRef &self, const Expr &other) {
                return UnevaluatedFuncRefExpr{self, other, UnevaluatedFuncRefExpr::Op::Div};
            });
    add_binary_operators(func_ref_class);
}

}  // namespace PythonBindings
}  // namespace Halide
