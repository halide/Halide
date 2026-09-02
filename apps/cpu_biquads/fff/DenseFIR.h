#ifndef SUPERBLURS_DENSEFIR_H
#define SUPERBLURS_DENSEFIR_H

#include "FloatVector.h"

#include <assert.h>
#include <vector>

template<int _num_taps,
         int _stride = 1,
         int _rate = 4,
         // use_shuffles = true is better if reading from registers,
         // use_shuffles = false is better if reading from memory
         bool use_shuffles = false,
         // Hide latencies in the += operations
         int num_independent_sums = 2,
         bool first_coefficient_is_one = false>
struct alignas(64) DenseFIR {
    constexpr static int rate = _rate;
    constexpr static int stride = _stride;
    constexpr static int num_taps = _num_taps;
    constexpr static int fmas_per_output = num_taps;
    constexpr static int num_prev = (num_taps * stride + vec_lanes - 1) / vec_lanes;

    static_assert(stride <= vec_lanes);

    constexpr static int circular_buffer_slop = rate > num_prev ? 0 : num_prev;
    float_vec buffer[num_prev + circular_buffer_slop] = {};
    int cursor = num_prev;
    constexpr static int cursor_limit = num_prev + circular_buffer_slop - rate;

    float taps[num_taps];

    void shape(size_t input_size, size_t *output_size) const {
        *output_size = input_size + (num_taps - 1) * stride;
    }

    __attribute__((always_inline)) void run(const float_vec *__restrict__ input, float_vec *__restrict__ output) {
        float_vec sum[rate][num_independent_sums] = {};

        float_vec *prev = buffer + cursor;

        // For large FIRs, we want a different loop nesting order depending on
        // whether or not we're using shuffles.
        int outer_loop_limit, outer_loop_step, inner_loop_limit, inner_loop_step;
        if (!use_shuffles) {
            outer_loop_limit = vec_lanes;
            outer_loop_step = stride;
            inner_loop_limit = num_taps * stride;
            inner_loop_step = vec_lanes;
        } else {
            outer_loop_limit = num_taps * stride;
            outer_loop_step = vec_lanes;
            inner_loop_limit = vec_lanes;
            inner_loop_step = stride;
        }

#pragma unroll
        for (int outer = 0; outer < outer_loop_limit; outer += outer_loop_step) {
#pragma unroll
            for (int inner = 0; inner < inner_loop_limit; inner += inner_loop_step) {
                if (inner + outer >= num_taps * stride) break;
                const int j = inner_loop_step == stride ? outer : inner;
                const int k = inner_loop_step == stride ? inner : outer;
                const int t = (j + k) / stride;

                constexpr bool keep_coefficients_in_registers = rate > 1 && num_taps <= 8;
                float c = keep_coefficients_in_registers ? taps[t] : load_coefficient(taps, t);

#pragma unroll
                for (int r = 0; r < rate; r++) {
                    int idx = r * vec_lanes - (j + k);
                    // const float_vec *ptr = idx >= 0 ? input : prev + num_prev;
                    const float_vec *ptr = idx >= 0 ? input : prev;
                    float_vec v;
                    if (idx >= 0 || idx <= -vec_lanes) {
                        // Load from entirely input or prev
                        if (use_shuffles) {
                            int slice_index = (-k) & (vec_lanes - 1);
                            float_vec a = ptr[idx >> log_vec_lanes];
                            float_vec b = {};
                            if (slice_index != 0) {
                                b = ptr[(idx >> log_vec_lanes) + 1];
                            }
                            v = extract_slice(a, b, slice_index);
                        } else {
                            std::memcpy(&v, (const float *)ptr + idx, sizeof(float_vec));
                        }
                    } else {
                        // Partial load from the end of prev and the start of input
                        float_vec a = prev[-1];
                        float_vec b = input[0];
                        v = extract_slice(a, b, idx + vec_lanes);
                    }
                    if (j == 0 && k < num_independent_sums) {
                        if (t == 0 && first_coefficient_is_one) {
                            sum[r][k & (num_independent_sums - 1)] = v;
                        } else {
                            sum[r][k & (num_independent_sums - 1)] = v * c;
                        }
                    } else {
                        sum[r][k & (num_independent_sums - 1)] += v * c;
                    }
                }
            }
        }

        for (int r = 0; r < rate; r++) {
            output[r] = sum[r][0];
            for (int j = 1; j < num_independent_sums; j++) {
                output[r] += sum[r][j];
            }
        }

        // Update prev
        if (circular_buffer_slop == 0) {
            // There's no slop in the circular buffer, so we have to reset it
            // every time.
            for (int i = 0; i + rate < num_prev; i++) {
                buffer[i] = buffer[i + rate];
            }
            for (int i = 0; i < rate; i++) {
                int j = num_prev - rate + i;
                if (j < 0) continue;
                buffer[j] = input[i];
            }
        } else {
            if (cursor > cursor_limit) {
                // num_prev might be large, and we're out of space in the circular
                // buffer. Move everything back to the start.
                for (int i = 0; i < num_prev; i++) {
                    ((volatile float_vec *)buffer)[i] = prev[i - num_prev];
                }
                cursor = num_prev;
                prev = buffer + cursor;
            }

#pragma unroll
            for (int i = 0; i < rate; i++) {
                // Push the new input into the buffer.
                prev[i] = input[i];
            }
            cursor += rate;
        }
    }

    void reset() {
        cursor = num_prev;
        std::memset(buffer, 0, num_prev * sizeof(float_vec));
    }

    DenseFIR(const std::vector<float> &t) {
        assert(t.size() == num_taps);
        std::memcpy(taps, &t[0], num_taps * sizeof(float));
        reset();
    }
};

template<int _num_taps, int _stride>
struct alignas(64) LargeDenseFIR {
    static constexpr int rate = 4;
    static constexpr int stride = _stride;
    static constexpr int num_taps = _num_taps;
    static constexpr int buffer_size = (stride * num_taps - 1) / vec_lanes + 1 + rate;
    static constexpr int fmas_per_output = num_taps;

    static_assert(stride <= vec_lanes);

    float_vec buffer[buffer_size];

    // The taps reordered into the order they're actually used in
    float swizzled_taps[num_taps];

    float taps[num_taps];

    void shape(size_t input_size, size_t *output_size) const {
        *output_size = input_size + (num_taps - 1) * stride;
    }

    __attribute__((always_inline)) void run(const float_vec *__restrict__ input, float_vec *__restrict__ output) {

        static_assert(vec_lanes % stride == 0);

        buffer[buffer_size - 4] = input[0];
        buffer[buffer_size - 3] = input[1];
        buffer[buffer_size - 2] = input[2];
        buffer[buffer_size - 1] = input[3];

        float_vec acc[8] = {};
        const float *t = swizzled_taps;
        // const float *taps_end = swizzled_taps + num_taps;
        // One beyond the last safe place to do a vector load from
        const unaligned_float_vec *ptr_end =
            (const unaligned_float_vec *)((const float *)(buffer + buffer_size - 1) + 1);
        for (int phase = 0; phase < vec_lanes; phase += stride) {
            const float *origin = (const float *)(buffer + buffer_size - 4);
            const unaligned_float_vec *ptr =
                (const unaligned_float_vec *)(origin - stride * (num_taps - 1) + phase);
            float_vec p0 = *ptr++;
            float_vec p1 = *ptr++;
            float_vec p2 = *ptr++;

#define INNER_LOOP(off)              \
    {                                \
        const float t0 = *t++;       \
        const float_vec p3 = *ptr++; \
        acc[off + 0] += t0 * p0;     \
        acc[off + 1] += t0 * p1;     \
        acc[off + 2] += t0 * p2;     \
        acc[off + 3] += t0 * p3;     \
        p0 = p1;                     \
        p1 = p2;                     \
        p2 = p3;                     \
    }

#pragma clang loop unroll(disable)
            while (ptr + 3 < ptr_end) {
                INNER_LOOP(0);
                INNER_LOOP(4);
                INNER_LOOP(0);
                INNER_LOOP(4);
            }

            if (ptr + 1 < ptr_end) {
                INNER_LOOP(0);
                INNER_LOOP(4);
            }

            if (ptr < ptr_end) {
                INNER_LOOP(0);
            }
        }

        // Move everything in the buffer back by 4 vectors
        // TODO: Consider moving it back further, to trade instructions for L1 usage.
        // TODO: Try fusing this into the above
        for (int i = 0; i + 4 < buffer_size; i++) {
            buffer[i] = buffer[i + 4];
        }

        output[0] = acc[0] + acc[4];
        output[1] = acc[1] + acc[5];
        output[2] = acc[2] + acc[6];
        output[3] = acc[3] + acc[7];
    }

    void reset() {
        std::memset(buffer, 0, sizeof(buffer));
    }

    LargeDenseFIR(const std::vector<float> &t) {
        assert(t.size() == num_taps);
        std::memcpy(taps, t.data(), num_taps * sizeof(float));
        reset();

        float *s_ptr = swizzled_taps;
        constexpr int t_step = -(vec_lanes / stride);
        for (int phase = 0; phase < vec_lanes; phase += stride) {
            const float *t = taps + num_taps - 1 - phase / stride;
            while (t >= taps) {
                *s_ptr++ = *t;
                t += t_step;
            }
        }
        assert(s_ptr == swizzled_taps + num_taps);
    }
};

#endif
