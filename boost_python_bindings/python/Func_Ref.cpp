#include "Func_Ref.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
//#include "add_operators.h"

#include "../../src/Func.h"

#include <vector>
#include <string>

namespace h = Halide;
namespace p = boost::python;
using p::self;


template<typename A, typename B>
auto add_func_refs(A &a, B &b) -> decltype(a + b)
{
    return a + b;
}

template<typename A, typename B>
auto sub_func_refs(A &a, B &b) -> decltype(a - b)
{
    return a - b;
}

template<typename A, typename B>
auto mul_func_refs(A &a, B &b) -> decltype(a * b)
{
    return a * b;
}

template<typename A, typename B>
auto div_func_refs(A &a, B &b) -> decltype(a / b)
{
    return a / b;
}

template<typename A, typename B>
auto mod_func_refs(A &a, B &b) -> decltype(a % b)
{
    return a % b;
}

template<typename A, typename B>
auto and_func_refs(A &a, B &b) -> decltype(a & b)
{
    return a & b;
}

template<typename A, typename B>
auto xor_func_refs(A &a, B &b) -> decltype(a ^ b)
{
    return a ^ b;
}

template<typename A, typename B>
auto or_func_refs(A &a, B &b) -> decltype(a | b)
{
    return a | b;
}

template<typename A, typename B>
auto gt_func_refs(A &a, B &b) -> decltype(a > b)
{
    return a > b;
}

template<typename A, typename B>
auto ge_func_refs(A &a, B &b) -> decltype(a >= b)
{
    return a >= b;
}

template<typename A, typename B>
auto lt_func_refs(A &a, B &b) -> decltype(a < b)
{
    return a < b;
}

template<typename A, typename B>
auto le_func_refs(A &a, B &b) -> decltype(a <= b)
{
    return a <= b;
}

template<typename A, typename B>
auto eq_func_refs(A &a, B &b) -> decltype(a == b)
{
    return a == b;
}

template<typename A, typename B>
auto ne_func_refs(A &a, B &b) -> decltype(a != b)
{
    return a != b;
}


template<typename PythonClass, typename B>
void add_func_ref_operators_with(PythonClass &class_a)
{
    typedef typename PythonClass::wrapped_type T;

    class_a
            .def("__add__", &add_func_refs<T, B>)
            .def("__sub__", &sub_func_refs<T, B>)
        #if PY_VERSION_HEX >= 0x03000000
            .def("__truediv__", &div_func_refs<T, B>)
        #else
            .def("__div__", &div_func_refs<T, B>)
        #endif
            .def("__mod__", &mod_func_refs<T, B>)
            .def("__and__", &and_func_refs<T, B>)
            .def("__xor__", &xor_func_refs<T, B>)
            .def("__or__", &or_func_refs<T, B>)
            .def("__gt__", &gt_func_refs<T, B>)
            .def("__ge__", &gt_func_refs<T, B>)
            .def("__lt__", &lt_func_refs<T, B>)
            .def("__le__", &le_func_refs<T, B>)
            .def("__eq__", &eq_func_refs<T, B>)
            .def("__ne__", &ne_func_refs<T, B>)
            ;

    //    BOOST_PYTHON_BINARY_OPERATOR(lshift, rlshift, <<)
    //    BOOST_PYTHON_BINARY_OPERATOR(rshift, rrshift, >>)

    return;
}

void defineFuncRef()
{
    using Halide::FuncRefVar;
    using Halide::FuncRefExpr;
    using p::self;

    // only defined so that boost::python knows about these classes,
    // not (yet) meant to be created or manipulated by the user
    auto func_ref_var_class =
            p::class_<FuncRefVar>("FuncRefVar", p::no_init)
            ;

    add_func_ref_operators_with<decltype(func_ref_var_class), FuncRefVar>(func_ref_var_class);
    add_func_ref_operators_with<decltype(func_ref_var_class), FuncRefExpr>(func_ref_var_class);

    auto func_ref_expr_class =
            p::class_<FuncRefExpr>("FuncRefExpr", p::no_init)
            ;

    add_func_ref_operators_with<decltype(func_ref_expr_class), FuncRefVar>(func_ref_expr_class);
    add_func_ref_operators_with<decltype(func_ref_expr_class), FuncRefExpr>(func_ref_expr_class);

    p::implicitly_convertible<FuncRefVar, h::Expr>();
    p::implicitly_convertible<FuncRefExpr, h::Expr>();

    return;
}

