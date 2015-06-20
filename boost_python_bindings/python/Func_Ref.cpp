#include "Func_Ref.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
//#include "add_operators.h"

#include "../../src/Func.h"
#include "../../src/Tuple.h"

#include <vector>
#include <string>

namespace h = Halide;
namespace p = boost::python;
using p::self;


template<typename A, typename B>
auto add_func(A a, B b) -> decltype(a + b)
{
    return a + b;
}

template<typename A, typename B>
auto sub_func(A a, B b) -> decltype(a - b)
{
    return a - b;
}

template<typename A, typename B>
auto mul_func(A a, B b) -> decltype(a * b)
{
    return a * b;
}

template<typename A, typename B>
auto div_func(A a, B b) -> decltype(a / b)
{
    return a / b;
}

template<typename A, typename B>
auto mod_func(A a, B b) -> decltype(a % b)
{
    return a % b;
}

template<typename A, typename B>
auto and_func(A a, B b) -> decltype(a & b)
{
    return a & b;
}

template<typename A, typename B>
auto xor_func(A a, B b) -> decltype(a ^ b)
{
    return a ^ b;
}

template<typename A, typename B>
auto or_func(A a, B b) -> decltype(a | b)
{
    return a | b;
}

template<typename A, typename B>
auto gt_func(A a, B b) -> decltype(a > b)
{
    return a > b;
}

template<typename A, typename B>
auto ge_func(A a, B b) -> decltype(a >= b)
{
    return a >= b;
}

template<typename A, typename B>
auto lt_func(A a, B b) -> decltype(a < b)
{
    return a < b;
}

template<typename A, typename B>
auto le_func(A a, B b) -> decltype(a <= b)
{
    return a <= b;
}

template<typename A, typename B>
auto eq_func(A a, B b) -> decltype(a == b)
{
    return a == b;
}

template<typename A, typename B>
auto ne_func(A a, B b) -> decltype(a != b)
{
    return a != b;
}

template<typename A, typename B>
auto lshift_func(A a, B b) -> decltype(a << b)
{
    return a << b;
}

template<typename A, typename B>
auto rshift_func(A a, B b) -> decltype(a >> b)
{
    return a >> b;
}


template<typename A, typename B>
auto iadd_func_refs(A &a, B &b) -> decltype(a += b)
{
    return a += b;
}

template<typename A, typename B>
auto isub_func_refs(A &a, B &b) -> decltype(a -= b)
{
    return a -= b;
}

template<typename A, typename B>
auto imul_func_refs(A &a, B &b) -> decltype(a *= b)
{
    return a *= b;
}

template<typename A, typename B>
auto idiv_func_refs(A &a, B &b) -> decltype(a /= b)
{
    return a /= b;
}


template<typename A, typename B, typename PythonClass>
void add_func_operators_with(PythonClass &class_a)
{
    typedef typename PythonClass::wrapped_type T;

    const bool t_matches_a_or_b = std::is_same<A, T>::value
            or std::is_same<A, T&>::value
            or std::is_same<B, T>::value
            or std::is_same<B, T&>::value;
    assert(t_matches_a_or_b);

    // <boost/python/operators.hpp> lists all operators
    class_a
            .def("__add__", &add_func<A, B>)
            .def("__sub__", &sub_func<A, B>)
        #if PY_VERSION_HEX >= 0x03000000
            .def("__truediv__", &div_func<A, B>)
        #else
            .def("__div__", &div_func<A, B>)
        #endif
            .def("__mod__", &mod_func<A, B>)
            .def("__and__", &and_func<A, B>)
            .def("__xor__", &xor_func<A, B>)
            .def("__or__", &or_func<A, B>)
            .def("__gt__", &gt_func<A, B>)
            .def("__ge__", &gt_func<A, B>)
            .def("__lt__", &lt_func<A, B>)
            .def("__le__", &le_func<A, B>)
            .def("__eq__", &eq_func<A, B>)
            .def("__ne__", &ne_func<A, B>)
            .def("__lshift__", &lshift_func<A, B>)
            .def("__rshift__", &rshift_func<A, B>)

            //    BOOST_PYTHON_INPLACE_OPERATOR(iadd,+=)
            //    BOOST_PYTHON_INPLACE_OPERATOR(isub,-=)
            //    BOOST_PYTHON_INPLACE_OPERATOR(imul,*=)
            //    BOOST_PYTHON_INPLACE_OPERATOR(idiv,/=)
            //    BOOST_PYTHON_INPLACE_OPERATOR(imod,%=)
            //    BOOST_PYTHON_INPLACE_OPERATOR(ilshift,<<=)
            //    BOOST_PYTHON_INPLACE_OPERATOR(irshift,>>=)
            //    BOOST_PYTHON_INPLACE_OPERATOR(iand,&=)
            //    BOOST_PYTHON_INPLACE_OPERATOR(ixor,^=)
            //    BOOST_PYTHON_INPLACE_OPERATOR(ior,|=)
            ;

    return;
}

void defineFuncRefVar()
{
    using Halide::FuncRefVar;
    using p::self;


    // only defined so that boost::python knows about these classes,
    // not (yet) meant to be created or manipulated by the user
    auto func_ref_var_class =
            p::class_<FuncRefVar>("FuncRefVar",
                                  "A fragment of front-end syntax of the form f(x, y, z), where x, "
                                  "y, z are Vars. It could be the left-hand side of a function "
                                  "definition, or it could be a call to a function. We don't know "
                                  "until we see how this object gets used.",
                                  p::no_init)
            //FuncRefVar(Internal::Function, const std::vector<Var> &, int placeholder_pos = -1);

            //    .def("__??__", FuncRefVar::operator=(Expr);
            //         "Use this as the left-hand-side of a definition.")
            //    .def("__??__", FuncRefVar::operator(const Tuple &),
            //         "Use this as the left-hand-side of an update definition for a "
            //         "Func with multiple outputs.")

            .def("__iadd__", &iadd_func_refs<FuncRefVar, h::Expr>,
                 "Define this function as a sum reduction over the given "
                 "expression. The expression should refer to some RDom to sum "
                 "over. If the function does not already have a pure definition, "
                 "this sets it to zero.")
            .def("__isub__", &isub_func_refs<FuncRefVar, h::Expr>,
                 "Define this function as a sum reduction over the negative of "
                 "the given expression. The expression should refer to some RDom "
                 "to sum over. If the function does not already have a pure "
                 "definition, this sets it to zero.")
            .def("__imul__", &imul_func_refs<FuncRefVar, h::Expr>,
                 "Define this function as a product reduction. The expression "
                 "should refer to some RDom to take the product over. If the "
                 "function does not already have a pure definition, this sets it "
                 "to 1.")
            .def("__idiv__", &idiv_func_refs<FuncRefVar, h::Expr>,
                 "Define this function as the product reduction over the inverse "
                 "of the expression. The expression should refer to some RDom to "
                 "take the product over. If the function does not already have a "
                 "pure definition, this sets it to 1.")

            //"Override the usual assignment operator, so that "
            //"f(x, y) = g(x, y) defines f."
            //Stage operator=(const FuncRefVar &);
            //Stage operator=(const FuncRefExpr &);
            //FIXME  implement __setitem__


            //    .def("to_Expr", &FuncRefVar::operator Expr,
            //         "Use this FuncRefVar as a call to the function, and not as the "
            //         "left-hand-side of a definition. Only works for single-output funcs.")

            .def("__getitem__", &FuncRefVar::operator [],
                 "When a FuncRefVar refers to a function that provides multiple "
                 "outputs, you can access each output as an Expr using "
                 "operator[].")
            .def("size", &FuncRefVar::size,
                 "How many outputs does the function this refers to produce.")
            .def("__len__", &FuncRefVar::size,
                 "How many outputs does the function this refers to produce.")

            .def("function", &FuncRefVar::function,
                 "What function is this calling?")
            ;

    typedef decltype(func_ref_var_class) func_ref_var_class_t;
    typedef func_ref_var_class_t frvc_t;
    add_func_operators_with<FuncRefVar &, FuncRefVar &, frvc_t>(func_ref_var_class);
    add_func_operators_with<FuncRefVar &, h::FuncRefExpr &, frvc_t>(func_ref_var_class);
    add_func_operators_with<FuncRefVar &, int, frvc_t>(func_ref_var_class);
    add_func_operators_with<FuncRefVar &, float, frvc_t>(func_ref_var_class);
    add_func_operators_with<int, FuncRefVar &, frvc_t>(func_ref_var_class);
    add_func_operators_with<float, FuncRefVar &, frvc_t>(func_ref_var_class);

    p::implicitly_convertible<FuncRefVar, h::Expr>();

    return;
}

void defineFuncRefExpr()
{
    using Halide::FuncRefExpr;
    using p::self;


    auto func_ref_expr_class =
            p::class_<FuncRefExpr>("FuncRefExpr",
                                   "A fragment of front-end syntax of the form f(x, y, z), where x, y, "
                                   "z are Exprs. If could be the left hand side of an update "
                                   "definition, or it could be a call to a function. We don't know "
                                   "until we see how this object gets used. ",
                                   p::no_init)
            //            FuncRefExpr(Internal::Function, const std::vector<Expr> &,
            //                        int placeholder_pos = -1);
            //            FuncRefExpr(Internal::Function, const std::vector<std::string> &,
            //                        int placeholder_pos = -1);

            //            .def("__??__", FuncRefExpr::operator=(Expr);
            //                 "Use this as the left-hand-side of an update definition (see "
            //                 "\\ref RDom). The function must already have a pure definition.")
            //            .def("__??__", FuncRefExpr::operator(const Tuple &),
            //                 "Use this as the left-hand-side of an update definition for a "
            //                 "Func with multiple outputs.")
            .def("__iadd__", &iadd_func_refs<FuncRefExpr, h::Expr>,
                 "Define this function as a sum reduction over the given "
                 "expression. The expression should refer to some RDom to sum "
                 "over. If the function does not already have a pure definition, "
                 "this sets it to zero.")
            .def("__isub__", &isub_func_refs<FuncRefExpr, h::Expr>,
                 "Define this function as a sum reduction over the negative of "
                 "the given expression. The expression should refer to some RDom "
                 "to sum over. If the function does not already have a pure "
                 "definition, this sets it to zero.")
            .def("__imul__", &imul_func_refs<FuncRefExpr, h::Expr>,
                 "Define this function as a product reduction. The expression "
                 "should refer to some RDom to take the product over. If the "
                 "function does not already have a pure definition, this sets it "
                 "to 1.")
            .def("__idiv__", &idiv_func_refs<FuncRefExpr, h::Expr>,
                 "Define this function as the product reduction over the inverse "
                 "of the expression. The expression should refer to some RDom to "
                 "take the product over. If the function does not already have a "
                 "pure definition, this sets it to 1.")

            //"Override the usual assignment operator, so that "
            //"f(x, y) = g(x, y) defines f."
            //Stage operator=(const FuncRefVar &);
            //Stage operator=(const FuncRefExpr &);
            //FIXME  implement __setitem__

            //            .def("to_Expr", &FuncRefExpr::operator Expr,
            //                 "Use this as a call to the function, and not the left-hand-side"
            //                 "of a definition. Only works for single-output Funcs.")

            .def("__getitem__", &FuncRefExpr::operator [],
                 "When a FuncRefExpr refers to a function that provides multiple "
                 "outputs, you can access each output as an Expr using "
                 "operator[].")
            .def("size", &FuncRefExpr::size,
                 "How many outputs does the function this refers to produce.")
            .def("__len__", &FuncRefExpr::size,
                 "How many outputs does the function this refers to produce.")

            .def("function", &FuncRefExpr::function,
                 "What function is this calling?")
            ;


    typedef decltype(func_ref_expr_class) func_ref_expr_class_t;
    typedef func_ref_expr_class_t frec_t;
    add_func_operators_with<FuncRefExpr &, h::FuncRefVar &, frec_t>(func_ref_expr_class);
    add_func_operators_with<FuncRefExpr &, FuncRefExpr &, frec_t>(func_ref_expr_class);
    add_func_operators_with<FuncRefExpr &, int, frec_t>(func_ref_expr_class);
    add_func_operators_with<FuncRefExpr &, float, frec_t>(func_ref_expr_class);
    add_func_operators_with<int, FuncRefExpr &, frec_t>(func_ref_expr_class);
    add_func_operators_with<float, FuncRefExpr &, frec_t>(func_ref_expr_class);

    p::implicitly_convertible<FuncRefExpr, h::Expr>();

    return;
}


void defineFuncRef()
{

    // only defined so that boost::python knows about these class,
    // not (yet) meant to be created or manipulated by the user
    p::class_<h::Internal::Function>("InternalFunction", p::no_init);

    defineFuncRefVar();
    defineFuncRefExpr();
    return;
}

