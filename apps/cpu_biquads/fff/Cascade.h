#ifndef SUPERBLURS_CASCADE_H
#define SUPERBLURS_CASCADE_H

#include "Allocator.h"
#include "FloatVector.h"
#include "NoOp.h"
#include "Util.h"

#include <algorithm>

template<typename A, typename B>
struct Cascade_ {
    A a;
    B b;

    static constexpr int rate = std::max(A::rate, B::rate);
    static constexpr int fmas_per_output = A::fmas_per_output + B::fmas_per_output;

    void shape(size_t input_size, size_t *output_size) const {
        size_t tmp;
        a.shape(input_size, &tmp);
        b.shape(tmp, output_size);
    }

    __attribute__((always_inline)) void run(const float_vec *input, float_vec *output) {
        float_vec a_output[rate];
        for (int i = 0; i < rate; i += A::rate) {
            a.run(&input[i], &a_output[i]);
        }
        for (int i = 0; i < rate; i += B::rate) {
            b.run(&a_output[i], &output[i]);
        }
    }

    void reset() {
        a.reset();
        b.reset();
    }

    Cascade_(A a, B b)
        : a(a), b(b) {
    }
};

template<typename First, typename... Stages>
auto Cascade(First first, Stages... rest) {
    return Cascade_<First, decltype(Cascade(rest...))>{first, Cascade(rest...)};
}

template<typename A>
auto Cascade(A a) {
    return a;
}

NoOp Cascade() {
    return NoOp();
}

template<typename A, typename B>
struct Cascade2D_ {
    A a;
    B b;

    void shape(size_t input_width, size_t input_height,
               size_t *output_width, size_t *output_height) const {
        size_t tmp_width, tmp_height;
        a.shape(input_width, input_height, &tmp_width, &tmp_height);
        b.shape(tmp_width, tmp_height, output_width, output_height);
    }

    template<OutputMode mode>
    void run_2d(const float *__restrict__ input,
                size_t input_width,
                size_t input_height,
                size_t input_stride,
                float *__restrict output,
                size_t output_width,
                size_t output_height,
                size_t output_stride,
                size_t min_x,
                size_t max_x,
                size_t min_y,
                size_t max_y) {

        dump_to_file("Cascade2DInput.tmp", input, input_width, input_height, input_stride);

        size_t tmp_width, tmp_height;
        a.shape(input_width, input_height, &tmp_width, &tmp_height);

        // Make a temporary allocation for the output of the first stage
        Alloc2D tmp(tmp_width, tmp_height, "Cascade2D");

        // TODO: If we had backwards bounds queries we could potentially only
        // compute an ROI of the first stage. Currently we're computing the
        // entire thing.

        // While we're computing the first child, the output can be used as scratch
        if (mode == Assign) {
            auto loaned = ScratchSpace(output, output_width, output_height, output_stride);
            a.template run_2d<Assign>(input, input_width, input_height, input_stride,
                                      tmp.ptr, tmp.width, tmp.height, tmp.stride,
                                      0, tmp.width, 0, tmp.height);
        } else {
            a.template run_2d<Assign>(input, input_width, input_height, input_stride,
                                      tmp.ptr, tmp.width, tmp.height, tmp.stride,
                                      0, tmp.width, 0, tmp.height);
        }

        dump_to_file("Cascade2DTmp.tmp", tmp.ptr, tmp.width, tmp.height, tmp.stride);

        b.template run_2d<mode>(tmp.ptr, tmp.width, tmp.height, tmp.stride,
                                output, output_width, output_height, output_stride,
                                min_x, max_x, min_y, max_y);

        dump_to_file("Cascade2DOutput.tmp", output, output_width, output_height, output_stride);
    }

    void reset() {
        a.reset();
        b.reset();
    }

    Cascade2D_(A a, B b)
        : a(a), b(b) {
    }
};

// Cascade nodes need temporary memory. They can offer the output buffer as
// temporary memory while computing the first stage. So we want to
// left-associate them so that the amount of temporary memory allocated stays
// constant. This results in ping-ponging between two buffers in long cascades.
template<typename First, typename Second, typename... Rest>
auto Cascade2D(First first, Second second, Rest... rest) {
    return Cascade2D(Cascade2D(first, second), rest...);
}

template<typename A, typename B>
auto Cascade2D(A a, B b) {
    return Cascade2D_<A, B>(a, b);
}

template<typename A>
auto Cascade2D(A a) {
    return a;
}

auto Cascade2D() {
    return NoOp();
}

#endif
