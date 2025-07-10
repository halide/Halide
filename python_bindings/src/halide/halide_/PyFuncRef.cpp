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

void define_func_ref(py::module &m) {
    auto stage_from_in_place_update_class =
        py::class_<StageFromInPlaceUpdate>(m, "_StageFromInPlaceUpdate")
            .def("__iadd__", [](StageFromInPlaceUpdate &self, const Expr &other) {
                return self.func_ref += other, self;
            })
            .def("__iadd__", [](StageFromInPlaceUpdate &self, const py::tuple &other) {
                return self.func_ref += to_halide_tuple(other), self;
            })
            .def("__iadd__", [](StageFromInPlaceUpdate &self, const FuncRef &other) {
                return self.func_ref += other, self;
            })
            .def("__isub__", [](StageFromInPlaceUpdate &self, const Expr &other) {
                return self.func_ref -= other, self;
            })
            .def("__isub__", [](StageFromInPlaceUpdate &self, const py::tuple &other) {
                return self.func_ref -= to_halide_tuple(other), self;
            })
            .def("__isub__", [](StageFromInPlaceUpdate &self, const FuncRef &other) {
                return self.func_ref -= other, self;
            })
            .def("__imul__", [](StageFromInPlaceUpdate &self, const Expr &other) {
                return self.func_ref *= other, self;
            })
            .def("__imul__", [](StageFromInPlaceUpdate &self, const py::tuple &other) {
                return self.func_ref *= to_halide_tuple(other), self;
            })
            .def("__imul__", [](StageFromInPlaceUpdate &self, const FuncRef &other) {
                return self.func_ref *= other, self;
            })
            .def("__itruediv__", [](StageFromInPlaceUpdate &self, const Expr &other) {
                return self.func_ref /= other, self;
            })
            .def("__itruediv__", [](StageFromInPlaceUpdate &self, const py::tuple &other) {
                return self.func_ref /= to_halide_tuple(other), self;
            })
            .def("__itruediv__", [](StageFromInPlaceUpdate &self, const FuncRef &other) {
                return self.func_ref /= other, self;
            });

    auto func_tuple_element_ref_class =
        py::class_<FuncTupleElementRef>(m, "FuncTupleElementRef")
            .def("index", &FuncTupleElementRef::index);

    add_binary_operators(func_tuple_element_ref_class);

    auto func_ref_class =
        py::class_<FuncRef>(m, "FuncRef")
            .def("__getitem__", &FuncRef::operator[])
            .def("size", &FuncRef::size)
            .def("__len__", &FuncRef::size)
            .def("__iadd__", [](FuncRef &self, const Expr &other) {
                return StageFromInPlaceUpdate{self += other, self};
            })
            .def("__iadd__", [](FuncRef &self, const py::tuple &other) {
                return StageFromInPlaceUpdate{self += to_halide_tuple(other), self};
            })
            .def("__iadd__", [](FuncRef &self, const FuncRef &other) {
                return StageFromInPlaceUpdate{self += other, self};
            })
            .def("__isub__", [](FuncRef &self, const Expr &other) {
                return StageFromInPlaceUpdate{self -= other, self};
            })
            .def("__isub__", [](FuncRef &self, const py::tuple &other) {
                return StageFromInPlaceUpdate{self -= to_halide_tuple(other), self};
            })
            .def("__isub__", [](FuncRef &self, const FuncRef &other) {
                return StageFromInPlaceUpdate{self -= other, self};
            })
            .def("__imul__", [](FuncRef &self, const Expr &other) {
                return StageFromInPlaceUpdate{self *= other, self};
            })
            .def("__imul__", [](FuncRef &self, const py::tuple &other) {
                return StageFromInPlaceUpdate{self *= to_halide_tuple(other), self};
            })
            .def("__imul__", [](FuncRef &self, const FuncRef &other) {
                return StageFromInPlaceUpdate{self *= other, self};
            })
            .def("__itruediv__", [](FuncRef &self, const Expr &other) {
                return StageFromInPlaceUpdate{self /= other, self};
            })
            .def("__itruediv__", [](FuncRef &self, const py::tuple &other) {
                return StageFromInPlaceUpdate{self /= to_halide_tuple(other), self};
            })
            .def("__itruediv__", [](FuncRef &self, const FuncRef &other) {
                return StageFromInPlaceUpdate{self /= other, self};
            });

    add_binary_operators(func_ref_class);
}

}  // namespace PythonBindings
}  // namespace Halide
