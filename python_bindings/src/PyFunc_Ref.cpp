#include "PyFunc_Ref.h"

#include "PyBinaryOperators.h"

namespace Halide {
namespace PythonBindings {

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

void define_func_tuple_element_ref() {
    auto func_tuple_element_ref_class =
        py::class_<FuncTupleElementRef>("FuncTupleElementRef",
                                       "A fragment of front-end syntax of the form f(x, y, z)[index], where x, "
                                       "y, z are Vars or Exprs. It could be the left-hand side of an update "
                                       "definition, or it could be a call to a function. We don't know "
                                       "until we see how this object gets used.",
                                       py::no_init)

            .def("__iadd__", &iadd_func<FuncTupleElementRef &, Expr>, py::args("self", "expr"),
                 py::return_internal_reference<1>(),
                 "Define a stage that adds the given expression to Tuple component 'idx' "
                 "of this Func. The other Tuple components are unchanged. If the expression "
                 "refers to some RDom, this performs a sum reduction of the expression over "
                 "the domain. The function must already have an initial definition.")
            .def("__isub__", &isub_func<FuncTupleElementRef &, Expr>, py::args("self", "expr"),
                 py::return_internal_reference<1>(),
                 "Define a stage that adds the negative of the given expression to Tuple "
                 "component 'idx' of this Func. The other Tuple components are unchanged. "
                 "If the expression refers to some RDom, this performs a sum reduction of "
                 "the negative of the expression over the domain. The function must already "
                 "have an initial definition.")
            .def("__imul__", &imul_func<FuncTupleElementRef &, Expr>, py::args("self", "expr"),
                 py::return_internal_reference<1>(),
                 "Define a stage that multiplies Tuple component 'idx' of this Func by "
                 "the given expression. The other Tuple components are unchanged. If the "
                 "expression refers to some RDom, this performs a product reduction of "
                 "the expression over the domain. The function must already have an "
                 "initial definition.")
            .def("__idiv__", &idiv_func<FuncTupleElementRef &, Expr>, py::args("self", "expr"),
                 py::return_internal_reference<1>(),
                 "Define a stage that divides Tuple component 'idx' of this Func by "
                 "the given expression. The other Tuple components are unchanged. "
                 "If the expression refers to some RDom, this performs a product "
                 "reduction of the inverse of the expression over the domain. The function "
                 "must already have an initial definition.")
            .def("function", &FuncTupleElementRef::function,
                 "What function is this calling?")
            .def("index", &FuncTupleElementRef::index,
                 "Return index to the function outputs.");

    add_binary_operators_with<FuncTupleElementRef>(func_tuple_element_ref_class);

    // Expr has empty constructor, thus self does the job
    // Expr will "eat" int and float arguments via implicit conversion
    add_binary_operators_with<Expr>(func_tuple_element_ref_class);

    py::implicitly_convertible<FuncTupleElementRef, Expr>();
}

void define_func_ref_expr_class() {
    auto func_ref_expr_class =
        py::class_<FuncRef>("FuncRef",
                           "A fragment of front-end syntax of the form f(x, y, z), where x, y, "
                           "z are Vars or Exprs. If could be the left hand side of a definition or an "
                           "update definition, or it could be a call to a function. We don't know "
                           "until we see how this object gets used. ",
                           py::no_init)
            .def("__iadd__", &iadd_func<FuncRef &, Expr>, py::args("self", "expr"),
                 py::return_internal_reference<1>(),
                 "Define a stage that adds the given expression to this Func. If the "
                 "expression refers to some RDom, this performs a sum reduction of the "
                 "expression over the domain. If the function does not already have a "
                 "pure definition, this sets it to zero.")
            .def("__isub__", &isub_func<FuncRef &, Expr>, py::args("self", "expr"),
                 py::return_internal_reference<1>(),
                 "Define a stage that adds the negative of the given expression to this "
                 "Func. If the expression refers to some RDom, this performs a sum reduction "
                 "of the negative of the expression over the domain. If the function does "
                 "not already have a pure definition, this sets it to zero.")
            .def("__imul__", &imul_func<FuncRef &, Expr>, py::args("self", "expr"),
                 py::return_internal_reference<1>(),
                 "Define a stage that multiplies this Func by the given expression. If the "
                 "expression refers to some RDom, this performs a product reduction of the "
                 "expression over the domain. If the function does not already have a pure "
                 "definition, this sets it to 1.")
            .def("__idiv__", &idiv_func<FuncRef &, Expr>, py::args("self", "expr"),
                 py::return_internal_reference<1>(),
                 "Define a stage that divides this Func by the given expression. "
                 "If the expression refers to some RDom, this performs a product "
                 "reduction of the inverse of the expression over the domain. If the "
                 "function does not already have a pure definition, this sets it to 1.")
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

    add_binary_operators_with<FuncRef>(func_ref_expr_class);

    // Expr has empty constructor, thus self does the job
    // Expr will "eat" int and float arguments via implicit conversion
    add_binary_operators_with<Expr>(func_ref_expr_class);

    py::implicitly_convertible<FuncRef, Expr>();
}

void define_func_ref() {
    // only defined so that boost::python knows about these class,
    // not (yet) meant to be created or manipulated by the user
    py::class_<Internal::Function> dummy("InternalFunction", py::no_init);

    define_func_tuple_element_ref();
    define_func_ref_expr_class();
}

}  // namespace PythonBindings
}  // namespace Halide
