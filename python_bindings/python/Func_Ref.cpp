#include "Func_Ref.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
#include "add_operators.h"

#include "../../src/Func.h"
#include "../../src/Tuple.h"

#include <vector>
#include <string>

namespace h = Halide;
namespace p = boost::python;


template<typename A, typename B>
A& iadd_func(A a, B b)
{
    a += b;
    // for FuncRefExpr this will create a stage,
    // but in python the return object replaces the caller,
    // and we do not want to change the caller
    return a;
}

template<typename A, typename B>
A& isub_func(A a, B b)
{
    a -= b;
    return a;
}

template<typename A, typename B>
A& imul_func(A a, B b)
{
    a *= b;
    return a;
}

template<typename A, typename B>
A& idiv_func(A a, B b)
{
    a /= b;
    return a;
}


h::Type func_ref_var_type(h::FuncRefVar &that)
{
    return static_cast<h::Expr>(that).type();
}


void defineFuncRefVar()
{
    using Halide::FuncRefVar;



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

            .def("__iadd__", &iadd_func<FuncRefVar &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
                 "Define this function as a sum reduction over the given "
                 "expression. The expression should refer to some RDom to sum "
                 "over. If the function does not already have a pure definition, "
                 "this sets it to zero.")
            .def("__isub__", &isub_func<FuncRefVar &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
                 "Define this function as a sum reduction over the negative of "
                 "the given expression. The expression should refer to some RDom "
                 "to sum over. If the function does not already have a pure "
                 "definition, this sets it to zero.")
            .def("__imul__", &imul_func<FuncRefVar &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
                 "Define this function as a product reduction. The expression "
                 "should refer to some RDom to take the product over. If the "
                 "function does not already have a pure definition, this sets it "
                 "to 1.")
            .def("__idiv__", &idiv_func<FuncRefVar &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
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

            .def("type", &func_ref_var_type,
                 "return type of the Expr version of FuncRefVar")

            .def("function", &FuncRefVar::function,
                 "What function is this calling?")
            ;

    typedef decltype(func_ref_var_class) func_ref_var_class_t;
    typedef func_ref_var_class_t frvc_t;

    p::implicitly_convertible<FuncRefVar, h::Expr>();

    // h::Expr has empty constructor, thus self does the job
    // h::Expr will "eat" int and float arguments via implicit conversion
    add_operators_with<frvc_t, h::Expr>(func_ref_var_class);

    add_operators_with<frvc_t, FuncRefVar>(func_ref_var_class);
    add_operators_with<frvc_t, h::FuncRefExpr>(func_ref_var_class);

    return;
}

void defineFuncRefExpr()
{
    using Halide::FuncRefExpr;



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
            .def("__iadd__", &iadd_func<FuncRefExpr &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
                 "Define this function as a sum reduction over the given "
                 "expression. The expression should refer to some RDom to sum "
                 "over. If the function does not already have a pure definition, "
                 "this sets it to zero.")
            .def("__isub__", &isub_func<FuncRefExpr &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
                 "Define this function as a sum reduction over the negative of "
                 "the given expression. The expression should refer to some RDom "
                 "to sum over. If the function does not already have a pure "
                 "definition, this sets it to zero.")
            .def("__imul__", &imul_func<FuncRefExpr &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
                 "Define this function as a product reduction. The expression "
                 "should refer to some RDom to take the product over. If the "
                 "function does not already have a pure definition, this sets it "
                 "to 1.")
            .def("__idiv__", &idiv_func<FuncRefExpr &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
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

    add_operators_with<frec_t, h::FuncRefVar>(func_ref_expr_class);
    add_operators_with<frec_t, FuncRefExpr>(func_ref_expr_class);

    // h::Expr has empty constructor, thus self does the job
    // h::Expr will "eat" int and float arguments via implicit conversion
    add_operators_with<frec_t, h::Expr>(func_ref_expr_class);

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

