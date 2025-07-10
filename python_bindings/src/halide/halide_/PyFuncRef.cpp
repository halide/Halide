#include "PyFuncRef.h"

#include "PyBinaryOperators.h"
#include "PyTuple.h"

namespace Halide {
namespace PythonBindings {

/*
 * The __iadd__ implementations here are meant to implement the special handling
 * of update definitions like the following:
 *
 *     Func f; RDom r(...);
 *     f(r) += ...;
 *
 * In this case, the type of the zero for f's pure definition is inferred from the
 * RHS. FuncRef's overloads of operator+= return a Stage. However, in Python, if
 * the __iadd__ (+=) method returns a new object, then __setitem__ will be called
 * to update the underlying object. In this case, it would try to call
 *
 *     f.__setitem__(r, <Stage>)
 *
 * where <Stage> is the result of FuncRef::operator+=. This doesn't make sense.
 * Instead, we return a special type (StageFromInPlaceUpdate) that signals to
 * our __setitem__ implementation that this is the case.
 */

namespace {

#define X_FuncRef(op, rhs) (StageFromInPlaceUpdate{self.operator op(rhs), self})
#define X_StageFromInPlaceUpdate(op, rhs) (self.func_ref.operator op(rhs), self)

// NOLINTBEGIN(bugprone-macro-parentheses)
#define DEF_IOP(T, pyop, op)                           \
    .def(pyop, [](T &self, const Expr &other) {        \
        return X##_##T(op, other);                     \
    }).def(pyop, [](T &self, const py::tuple &other) { \
          return X##_##T(op, to_halide_tuple(other));  \
      }).def(pyop, [](T &self, const FuncRef &other) { \
        return X##_##T(op, other);                     \
    })
// NOLINTEND(bugprone-macro-parentheses)

#define DEF_IOPS(cls, T)                    \
    do {                                    \
        cls DEF_IOP(T, "__iadd__", +=);     \
        cls DEF_IOP(T, "__isub__", -=);     \
        cls DEF_IOP(T, "__imul__", *=);     \
        cls DEF_IOP(T, "__itruediv__", /=); \
    } while (0)

}  // namespace

void define_func_ref(py::module &m) {
    auto stage_from_in_place_update_class =
        py::class_<StageFromInPlaceUpdate>(m, "_StageFromInPlaceUpdate");
    DEF_IOPS(stage_from_in_place_update_class, StageFromInPlaceUpdate);

    auto func_tuple_element_ref_class =
        py::class_<FuncTupleElementRef>(m, "FuncTupleElementRef")
            .def("index", &FuncTupleElementRef::index);
    add_binary_operators(func_tuple_element_ref_class);

    auto func_ref_class =
        py::class_<FuncRef>(m, "FuncRef")
            .def("__getitem__", &FuncRef::operator[])
            .def("size", &FuncRef::size)
            .def("__len__", &FuncRef::size);
    DEF_IOPS(func_ref_class, FuncRef);
    add_binary_operators(func_ref_class);
}

}  // namespace PythonBindings
}  // namespace Halide
