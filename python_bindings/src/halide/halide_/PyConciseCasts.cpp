#include "PyConciseCasts.h"

namespace Halide {
namespace PythonBindings {

void define_concise_casts(py::module &m) {
    // explicit cast should be tried before
    //  the pybind11::implicitly_convertible<T, Expr> conversion
    m.def("f64", [](double v) -> Expr {
        return Expr(v);
    });
    m.def("f32", [](float v) -> Expr {
        return Expr(v);
    });
    m.def("i64", [](int64_t v) -> Expr {
        return Expr(v);
    });
    m.def("i32", [](int32_t v) -> Expr {
        return Expr(v);
    });
    m.def("i16", [](int16_t v) -> Expr {
        return Expr(v);
    });
    m.def("i8", [](int8_t v) -> Expr {
        return Expr(v);
    });
    m.def("u64", [](uint64_t v) -> Expr {
        return Expr(v);
    });
    m.def("u32", [](uint32_t v) -> Expr {
        return Expr(v);
    });
    m.def("u16", [](uint16_t v) -> Expr {
        return Expr(v);
    });
    m.def("u8", [](uint8_t v) -> Expr {
        return Expr(v);
    });
    // pybind11::implicitly_convertible<T, Expr> conversions
    m.def("f64", Halide::ConciseCasts::f64);
    m.def("f32", Halide::ConciseCasts::f32);
    m.def("i64", Halide::ConciseCasts::i64);
    m.def("i32", Halide::ConciseCasts::i32);
    m.def("i16", Halide::ConciseCasts::i16);
    m.def("i8", Halide::ConciseCasts::i8);
    m.def("u64", Halide::ConciseCasts::u64);
    m.def("u32", Halide::ConciseCasts::u32);
    m.def("u16", Halide::ConciseCasts::u16);
    m.def("u8", Halide::ConciseCasts::u8);
    m.def("i8_sat", Halide::ConciseCasts::i8_sat);
    m.def("u8_sat", Halide::ConciseCasts::u8_sat);
    m.def("i16_sat", Halide::ConciseCasts::i16_sat);
    m.def("u16_sat", Halide::ConciseCasts::u16_sat);
    m.def("i32_sat", Halide::ConciseCasts::i32_sat);
    m.def("u32_sat", Halide::ConciseCasts::u32_sat);
    m.def("i64_sat", Halide::ConciseCasts::i64_sat);
    m.def("u64_sat", Halide::ConciseCasts::u64_sat);
}

}  // namespace PythonBindings
}  // namespace Halide
