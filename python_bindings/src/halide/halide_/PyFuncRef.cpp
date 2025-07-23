#include "PyFuncRef.h"

#include "PyBinaryOperators.h"

namespace Halide {
namespace PythonBindings {

template<typename T, typename = std::enable_if_t<!std::is_same_v<T, UnevaluatedFuncRefExpr>>>
Expr operator-(const UnevaluatedFuncRefExpr &lhs, const T &rhs) {
    return Expr(lhs) - rhs;
}

template<typename T>
Expr operator-(const T &lhs, const UnevaluatedFuncRefExpr &rhs) {
    return lhs - Expr(rhs);
}

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
            .def("__len__", &FuncRef::size);
    add_binary_operators(func_ref_class);
}

}  // namespace PythonBindings
}  // namespace Halide
