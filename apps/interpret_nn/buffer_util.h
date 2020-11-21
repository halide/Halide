#ifndef BUFFER_UTIL_H
#define BUFFER_UTIL_H

#include <iostream>
#include <random>

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "error_util.h"

namespace interpret_nn {

// Must be constexpr to allow use in case clauses.
inline constexpr int halide_type_code(halide_type_code_t code, int bits) {
    return (((int)code) << 8) | bits;
}

// dynamic_type_dispatch is a utility for functors that want to be able
// to dynamically dispatch a halide_type_t to type-specialized code.
// To use it, a functor must be a *templated* class, e.g.
//
//     template<typename T> class MyFunctor { int operator()(arg1, arg2...); };
//
// dynamic_type_dispatch() is called with a halide_type_t as the first argument,
// followed by the arguments to the Functor's operator():
//
//     auto result = dynamic_type_dispatch<MyFunctor>(some_halide_type, arg1, arg2);
//
// Note that this means that the functor must be able to instantiate its
// operator() for all the Halide scalar types; it also means that all those
// variants *will* be instantiated (increasing code size), so this approach
// should only be used when strictly necessary.
template<template<typename> class Functor, typename... Args>
auto dynamic_type_dispatch(const halide_type_t &type, Args &&... args)
    -> decltype(std::declval<Functor<uint8_t>>()(std::forward<Args>(args)...)) {

#define HANDLE_CASE(CODE, BITS, TYPE)  \
    case halide_type_code(CODE, BITS): \
        return Functor<TYPE>()(std::forward<Args>(args)...);
    switch (halide_type_code((halide_type_code_t)type.code, type.bits)) {
        // HANDLE_CASE(halide_type_float, 16, float)  // TODO
        HANDLE_CASE(halide_type_float, 32, float)
        HANDLE_CASE(halide_type_float, 64, double)
        HANDLE_CASE(halide_type_int, 8, int8_t)
        HANDLE_CASE(halide_type_int, 16, int16_t)
        HANDLE_CASE(halide_type_int, 32, int32_t)
        HANDLE_CASE(halide_type_int, 64, int64_t)
        HANDLE_CASE(halide_type_uint, 1, bool)
        HANDLE_CASE(halide_type_uint, 8, uint8_t)
        HANDLE_CASE(halide_type_uint, 16, uint16_t)
        HANDLE_CASE(halide_type_uint, 32, uint32_t)
        HANDLE_CASE(halide_type_uint, 64, uint64_t)
        // Omitted because we don't expect to see this here and adding would
        // require handling pointer types in our functors
        // HANDLE_CASE(halide_type_handle, 64, void *)
    default:
        LOG_FATAL << "Unsupported type";
        using ReturnType = decltype(std::declval<Functor<uint8_t>>()(std::forward<Args>(args)...));
        return ReturnType();
    }
#undef HANDLE_CASE
}

// Functor for use with dynamic_type_dispatch() to compare two buffers.
// Assumes that the buffers have the same type and same shape.
// TODO: assert-fails if type mismatch, but doesn't check shape.
template<typename T>
struct CompareBuffers {
    uint64_t operator()(const Halide::Runtime::Buffer<const void> &expected_buf_dynamic,
                        const Halide::Runtime::Buffer<const void> &actual_buf_dynamic) {
        Halide::Runtime::Buffer<const T> expected_buf = expected_buf_dynamic;
        Halide::Runtime::Buffer<const T> actual_buf = actual_buf_dynamic;
        uint64_t diffs = 0;
        constexpr uint64_t max_diffs_to_show = 32;
        expected_buf.for_each_element([&](const int *pos) {
            T expected_buf_val = expected_buf(pos);
            T actual_buf_val = actual_buf(pos);
            // TODO: this is terrible, we should compare with some threshold instead of equality
            if (expected_buf_val != actual_buf_val) {
                diffs++;
                if (diffs > max_diffs_to_show) {
                    return;
                }
                std::cerr << "*** Mismatch at (";
                for (int i = 0; i < expected_buf.dimensions(); ++i) {
                    if (i > 0) std::cerr << ", ";
                    std::cerr << pos[i];
                }
                std::cerr << "): expected " << 0 + expected_buf_val << " actual " << 0 + actual_buf_val << "\n";
            }
        });
        if (diffs > max_diffs_to_show) {
            std::cerr << "(" << (diffs - max_diffs_to_show) << " diffs suppressed)\n";
        }
        return diffs;
    }
};

// Functor for use with dynamic_type_dispatch() to fill a buffer
// with pseudorandom data.
template<typename T>
struct FillWithRandom {
    void operator()(Halide::Runtime::Buffer<> &b_dynamic, int seed) {
        Halide::Runtime::Buffer<T> b = b_dynamic;
        std::mt19937 rng(seed);
        fill_with_random_impl<T>(b, rng);
    }

private:
    template<typename T2 = T>
    static void fill_with_random_impl(Halide::Runtime::Buffer<T2> &b, std::mt19937 &rng) {
        std::uniform_int_distribution<T2> dis(std::numeric_limits<T2>::min(),
                                              std::numeric_limits<T2>::max());
        b.for_each_value([&rng, &dis](T2 &value) {
            value = dis(rng);
        });
    }

    template<>
    static void fill_with_random_impl(Halide::Runtime::Buffer<float> &b, std::mt19937 &rng) {
        // Floating point. We arbitrarily choose to use the range [0.0, 1.0].
        std::uniform_real_distribution<float> dis(0.0, 1.0);
        b.for_each_value([&rng, &dis](float &value) {
            value = dis(rng);
        });
    }

    template<>
    static void fill_with_random_impl(Halide::Runtime::Buffer<double> &b, std::mt19937 &rng) {
        // Floating point. We arbitrarily choose to use the range [0.0, 1.0].
        std::uniform_real_distribution<double> dis(0.0, 1.0);
        b.for_each_value([&rng, &dis](double &value) {
            value = dis(rng);
        });
    }

    template<>
    static void fill_with_random_impl(Halide::Runtime::Buffer<bool> &b, std::mt19937 &rng) {
        std::uniform_int_distribution<int> dis(0, 1);
        b.for_each_value([&rng, &dis](bool &value) {
            value = static_cast<bool>(dis(rng));
        });
    }
};

// Functor for use with dynamic_type_dispatch() to dump a buffer's contents
// to std::cerr in a very simple way. Intended only for temporary debugging.
template<typename T>
struct DumpBuffer {
    void operator()(const Halide::Runtime::Buffer<const void> &buf_dynamic) {
        Halide::Runtime::Buffer<const T> buf = buf_dynamic;
        buf.for_each_element([&](const int *pos) {
            T val = buf(pos);
            std::cerr << "Value at (";
            for (int i = 0; i < buf.dimensions(); ++i) {
                if (i > 0) std::cerr << ", ";
                std::cerr << pos[i];
            }
            std::cerr << "): " << 0 + val << "\n";
        });
    }
};

}  // namespace interpret_nn

#endif  // BUFFER_UTIL_H
