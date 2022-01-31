#ifndef HANNK_BUFFER_UTIL_H
#define HANNK_BUFFER_UTIL_H

#include <iostream>
#include <random>

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "interpreter/interval.h"
#include "util/error_util.h"

namespace hannk {

// Using a Buffer with space for max_rank dimensions is a meaningful
// win for some corner cases (when adding dimensions to > 4).
template<typename T, int Dims = Halide::Runtime::AnyDims>
using HalideBuffer = Halide::Runtime::Buffer<T, Dims, max_rank>;

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
auto dynamic_type_dispatch(const halide_type_t &type, Args &&...args)
    -> decltype(std::declval<Functor<uint8_t>>()(std::forward<Args>(args)...)) {

#define HANDLE_CASE(CODE, BITS, TYPE)        \
    case halide_type_t(CODE, BITS).as_u32(): \
        return Functor<TYPE>()(std::forward<Args>(args)...);

    switch (type.element_of().as_u32()) {
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
        HCHECK(0) << "Unsupported type";
        using ReturnType = decltype(std::declval<Functor<uint8_t>>()(std::forward<Args>(args)...));
        return ReturnType();
    }

#undef HANDLE_CASE
}

inline void check_shapes_match(const HalideBuffer<const void> &a,
                               const HalideBuffer<const void> &b) {
    HCHECK(a.dimensions() == b.dimensions());
    for (int d = 0; d < a.dimensions(); d++) {
        HCHECK(a.dim(d).min() == b.dim(d).min());
        HCHECK(a.dim(d).extent() == b.dim(d).extent());
    }
}

struct CompareBuffersOptions {
    // Threshold at which values are an 'exact' match.
    // For integral types this should always be 0.0.
    // For FP types it should be a small epsilon.
    double exact_thresh = 0.0;
    // Threshold at which values are 'close enough' to be considered ok
    // some part of the time.
    // For integral types this should always be 1.0.
    // For FP types it should be an epsilon.
    double close_thresh = 1.0;
    // What percentage (0..1) of elements in the result can be off by
    // more than exact_thresh (but <= close_thresh) and still have the
    // result be considered correct.
    double max_close_percent = 0.001;  // 0.1% by default. TODO: tweak as needed
    // If true, log info about failures to stderr.
    // If false, log nothing, stay silent.
    bool verbose = true;
    // Max number of diffs to log
    uint64_t max_diffs_to_log = 32;  // somewhat arbitrary

    void require_exact() {
        exact_thresh = 0.0;
        close_thresh = 0.0;
        max_close_percent = 0.0;
    }
};

struct CompareBuffersResult {
    uint64_t num_close = 0;
    uint64_t num_wrong = 0;
    bool ok = true;
};

// Functor for use with dynamic_type_dispatch() to compare two buffers.
// Requires that the buffers have the same type and same shape (ignoring strides);
// type/shape mismatch will check-fail immediately.
template<typename T>
struct CompareBuffers {
    CompareBuffersResult operator()(const HalideBuffer<const void> &expected_buf_dynamic,
                                    const HalideBuffer<const void> &actual_buf_dynamic,
                                    const CompareBuffersOptions &opts) {
        HalideBuffer<const T> expected_buf = expected_buf_dynamic;
        HalideBuffer<const T> actual_buf = actual_buf_dynamic;
        check_shapes_match(expected_buf, actual_buf);

        assert(opts.exact_thresh >= 0.0);
        assert(opts.close_thresh >= opts.exact_thresh);
        assert(opts.max_close_percent >= 0.0 && opts.max_close_percent <= 1.0);
        const T exact_thresh = (T)opts.exact_thresh;
        const T close_thresh = (T)opts.close_thresh;

        const uint64_t max_close = std::ceil(expected_buf.number_of_elements() * opts.max_close_percent);

        const auto do_compare = [&](bool verbose) -> CompareBuffersResult {
            CompareBuffersResult r;
            expected_buf.for_each_element([&](const int *pos) {
                T expected = expected_buf(pos);
                T actual = actual_buf(pos);
                T diff = (expected > actual) ? (expected - actual) : (actual - expected);
                if (diff > close_thresh) {
                    bool do_log = verbose;
                    const char *msg;
                    if (diff > exact_thresh) {
                        r.num_wrong++;
                        do_log &= (r.num_wrong < opts.max_diffs_to_log);
                        msg = "WRONG";
                    } else {
                        r.num_close++;
                        do_log &= (r.num_close < opts.max_diffs_to_log);
                        msg = "Inexact";
                    }
                    if (do_log) {
                        std::cerr << "*** " << msg << " at (";
                        for (int i = 0; i < expected_buf.dimensions(); ++i) {
                            if (i > 0) std::cerr << ", ";
                            std::cerr << pos[i];
                        }
                        std::cerr << "): expected " << 0 + expected << " actual " << 0 + actual << " diff " << 0 + diff << "\n";
                    }
                }
            });
            return r;
        };

        CompareBuffersResult r = do_compare(false);
        if (r.num_wrong > 0 || r.num_close > max_close) {
            r.ok = false;
        }
        if (opts.verbose) {
            if (!r.ok) {
                // Run again just to log the diffs
                std::cerr << "*** TOO MANY WRONG/INEXACT ELEMENTS (wrong " << r.num_wrong
                          << ", close " << r.num_close << " vs " << max_close << "):\n";
                (void)do_compare(true);
            }
            if (r.num_wrong > opts.max_diffs_to_log) {
                std::cerr << "(" << (r.num_wrong - opts.max_diffs_to_log) << " wrong values omitted)\n";
            }
            if (r.num_close > opts.max_diffs_to_log) {
                std::cerr << "(" << (r.num_close - opts.max_diffs_to_log) << " inexact values omitted)\n";
            }
        }
        return r;
    }
};

// Functor for use with dynamic_type_dispatch() to fill a buffer
// with pseudorandom data.
template<typename T>
struct FillWithRandom {
    void operator()(HalideBuffer<void> &b_dynamic, int seed) {
        HalideBuffer<T> b = b_dynamic;
        std::mt19937 rng(seed);
        fill_with_random_impl(b, rng);
    }

private:
    inline static void fill_with_random_impl(HalideBuffer<T> &b, std::mt19937 &rng) {
        std::uniform_int_distribution<T> dis(std::numeric_limits<T>::min(),
                                             std::numeric_limits<T>::max());
        b.for_each_value([&rng, &dis](T &value) {
            value = dis(rng);
        });
    }
};

// Specializations must be at namespace scope, not class scope
template<>
inline /*static*/ void FillWithRandom<float>::fill_with_random_impl(HalideBuffer<float> &b, std::mt19937 &rng) {
    // Floating point. We arbitrarily choose to use the range [0.0, 1.0].
    std::uniform_real_distribution<float> dis(0.0, 1.0);
    b.for_each_value([&rng, &dis](float &value) {
        value = dis(rng);
    });
}

template<>
inline /*static*/ void FillWithRandom<double>::fill_with_random_impl(HalideBuffer<double> &b, std::mt19937 &rng) {
    // Floating point. We arbitrarily choose to use the range [0.0, 1.0].
    std::uniform_real_distribution<double> dis(0.0, 1.0);
    b.for_each_value([&rng, &dis](double &value) {
        value = dis(rng);
    });
}

template<>
inline /*static*/ void FillWithRandom<bool>::fill_with_random_impl(HalideBuffer<bool> &b, std::mt19937 &rng) {
    std::uniform_int_distribution<int> dis(0, 1);
    b.for_each_value([&rng, &dis](bool &value) {
        value = static_cast<bool>(dis(rng));
    });
}

// Functor for use with dynamic_type_dispatch() to dump a buffer's contents
// to std::cerr in a very simple way. Intended only for temporary debugging.
template<typename T>
struct DumpBuffer {
    void operator()(const HalideBuffer<const void> &buf_dynamic) {
        HalideBuffer<const T> buf = buf_dynamic;
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

}  // namespace hannk

#endif  // HANNK_BUFFER_UTIL_H
