#include "Func_Ref.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include "add_operators.h"
#include <boost/python.hpp>

#include "Halide.h"

#include <string>
#include <vector>

namespace h = Halide;
namespace p = boost::python;

template <typename A, typename B>
A &iadd_func(A a, B b) {
    a += b;
    // for FuncRef this will create a stage,
    // but in python the return object replaces the caller,
    // and we do not want to change the caller
    return a;
}

template <typename A, typename B>
A &isub_func(A a, B b) {
    a -= b;
    return a;
}

template <typename A, typename B>
A &imul_func(A a, B b) {
    a *= b;
    return a;
}

template <typename A, typename B>
A &idiv_func(A a, B b) {
    a /= b;
    return a;
}

void defineFuncTupleElementRef() {
    using Halide::FuncTupleElementRef;

    auto func_tuple_element_ref_class =
        p::class_<FuncTupleElementRef>("FuncTupleElementRef",
                                       "A fragment of front-end syntax of the form f(x, y, z)[index], where x, "
                                       "y, z are Vars or Exprs. It could be the left-hand side of an update "
                                       "definition, or it could be a call to a function. We don't know "
                                       "until we see how this object gets used.",
                                       p::no_init)
            //FuncTupleElementRef(const FuncRef &ref, const std::vector<Expr>& args, int idx);

            //    .def("__??__", FuncTupleElementRef::operator=(Expr);
            //         "Use this as the left-hand-side of an update definition of Tuple "
            //         "component 'idx' of a Func (see \ref RDom). The function must "
            //         "already have an initial definition.")

            .def("__iadd__", &iadd_func<FuncTupleElementRef &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
                 "Define a stage that adds the given expression to Tuple component 'idx' "
                 "of this Func. The other Tuple components are unchanged. If the expression "
                 "refers to some RDom, this performs a sum reduction of the expression over "
                 "the domain. The function must already have an initial definition.")
            .def("__isub__", &isub_func<FuncTupleElementRef &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
                 "Define a stage that adds the negative of the given expression to Tuple "
                 "component 'idx' of this Func. The other Tuple components are unchanged. "
                 "If the expression refers to some RDom, this performs a sum reduction of "
                 "the negative of the expression over the domain. The function must already "
                 "have an initial definition.")
            .def("__imul__", &imul_func<FuncTupleElementRef &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
                 "Define a stage that multiplies Tuple component 'idx' of this Func by "
                 "the given expression. The other Tuple components are unchanged. If the "
                 "expression refers to some RDom, this performs a product reduction of "
                 "the expression over the domain. The function must already have an "
                 "initial definition.")
            .def("__idiv__", &idiv_func<FuncTupleElementRef &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
                 "Define a stage that divides Tuple component 'idx' of this Func by "
                 "the given expression. The other Tuple components are unchanged. "
                 "If the expression refers to some RDom, this performs a product "
                 "reduction of the inverse of the expression over the domain. The function "
                 "must already have an initial definition.")

            //"Override the usual assignment operator, so that "
            //"f(x, y)[index] = g(x, y) defines f."
            //Stage operator=(const FuncRef &);
            //FIXME  implement __setitem__

            //    .def("to_Expr", &FuncTupleElementRef::operator Expr,
            //         "Use this as a call to Tuple component 'idx' of a Func, and not the "
            //         "left-hand-side of a definition.")

            .def("function", &FuncTupleElementRef::function,
                 "What function is this calling?")
            .def("index", &FuncTupleElementRef::index,
                 "Return index to the function outputs.");

    typedef decltype(func_tuple_element_ref_class) func_tuple_element_ref_class_t;
    typedef func_tuple_element_ref_class_t fterc_t;

    add_operators_with<fterc_t, FuncTupleElementRef>(func_tuple_element_ref_class);

    // h::Expr has empty constructor, thus self does the job
    // h::Expr will "eat" int and float arguments via implicit conversion
    add_operators_with<fterc_t, h::Expr>(func_tuple_element_ref_class);

    p::implicitly_convertible<FuncTupleElementRef, h::Expr>();

    return;
}

void defineFuncRefExprClass() {
    using Halide::FuncRef;

    auto func_ref_expr_class =
        p::class_<FuncRef>("FuncRef",
                           "A fragment of front-end syntax of the form f(x, y, z), where x, y, "
                           "z are Vars or Exprs. If could be the left hand side of a definition or an "
                           "update definition, or it could be a call to a function. We don't know "
                           "until we see how this object gets used. ",
                           p::no_init)
            //            FuncRef(Internal::Function, const std::vector<Expr> &,
            //                        int placeholder_pos = -1);
            //            FuncRef(Internal::Function, const std::vector<Var> &,
            //                        int placeholder_pos = -1);

            //            .def("__??__", FuncRef::operator=(Expr);
            //                 "Use this as the left-hand-side of a definition or an update "
            //                 "definition (see \\ref RDom).")
            //            .def("__??__", FuncRef::operator(const Tuple &),
            //                 "Use this as the left-hand-side of a definition or an update "
            //                 "definition for a Func with multiple outputs.")
            .def("__iadd__", &iadd_func<FuncRef &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
                 "Define a stage that adds the given expression to this Func. If the "
                 "expression refers to some RDom, this performs a sum reduction of the "
                 "expression over the domain. If the function does not already have a "
                 "pure definition, this sets it to zero.")
            .def("__isub__", &isub_func<FuncRef &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
                 "Define a stage that adds the negative of the given expression to this "
                 "Func. If the expression refers to some RDom, this performs a sum reduction "
                 "of the negative of the expression over the domain. If the function does "
                 "not already have a pure definition, this sets it to zero.")
            .def("__imul__", &imul_func<FuncRef &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
                 "Define a stage that multiplies this Func by the given expression. If the "
                 "expression refers to some RDom, this performs a product reduction of the "
                 "expression over the domain. If the function does not already have a pure "
                 "definition, this sets it to 1.")
            .def("__idiv__", &idiv_func<FuncRef &, h::Expr>, p::args("self", "expr"),
                 p::return_internal_reference<1>(),
                 "Define a stage that divides this Func by the given expression. "
                 "If the expression refers to some RDom, this performs a product "
                 "reduction of the inverse of the expression over the domain. If the "
                 "function does not already have a pure definition, this sets it to 1.")

            //"Override the usual assignment operator, so that "
            //"f(x, y) = g(x, y) defines f."
            //Stage operator=(const FuncRef &);
            //FIXME  implement __setitem__

            //            .def("to_Expr", &FuncRef::operator Expr,
            //                 "Use this as a call to the function, and not the left-hand-side"
            //                 "of a definition. Only works for single-output Funcs.")

            .def("__getitem__", &FuncRef::operator[],
                 "When a FuncRef refers to a function that provides multiple "
                 "outputs, you can access each output as an Expr using "
                 "operator[].")
            .def("size", &FuncRef::size,
                 "How many outputs does the function this refers to produce.")
            .def("__len__", &FuncRef::size,
                 "How many outputs does the function this refers to produce.")

            .def("function", &FuncRef::function,
                 "What function is this calling?");

    typedef decltype(func_ref_expr_class) func_ref_expr_class_t;
    typedef func_ref_expr_class_t frec_t;

    add_operators_with<frec_t, FuncRef>(func_ref_expr_class);

    // h::Expr has empty constructor, thus self does the job
    // h::Expr will "eat" int and float arguments via implicit conversion
    add_operators_with<frec_t, h::Expr>(func_ref_expr_class);

    p::implicitly_convertible<FuncRef, h::Expr>();

    return;
}

void defineFuncRef() {
    // only defined so that boost::python knows about these class,
    // not (yet) meant to be created or manipulated by the user
    p::class_<h::Internal::Function>("InternalFunction", p::no_init);

    defineFuncTupleElementRef();
    defineFuncRefExprClass();
    return;
}
