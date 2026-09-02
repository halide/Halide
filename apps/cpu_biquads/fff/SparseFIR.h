#ifndef SUPERBLURS_SPARSEFIR_H
#define SUPERBLURS_SPARSEFIR_H

#include <algorithm>

template<typename... Args>
constexpr int get_max(Args... args) {
    int arr[] = {args...};
    int max = arr[0];
    for (int i = 1; i < sizeof...(args); i++) {
        max = std::max(max, arr[i]);
    }
    return max;
}

template<int desired_rate, int... tap_locs>
struct alignas(64) SparseFIR {
    static constexpr int rate = desired_rate;
    static constexpr int num_taps = (int)(sizeof...(tap_locs));
    static constexpr int fmas_per_output = num_taps;
    static constexpr int max_tap = get_max(tap_locs...);

    float coeffs[num_taps];

    CircularBuffer<max_tap + rate * vec_lanes> buffer;

    void shape(size_t input_size, size_t *output_size) const {
        *output_size = input_size + max_tap;
    }

    void run(const float_vec *input, float_vec *output) {
        constexpr int tap_locs_array[] = {tap_locs...};

        constexpr int first_idx = tap_locs_array[0] == 0 ? 1 : 0;

        float_vec sum[rate] = {};
        if (tap_locs_array[0] == 0) {
            for (int r = 0; r < rate; r++) {
                sum[r] = input[r] * coeffs[0];
            }
        }

        buffer.push(input, rate);

        for (int i = first_idx; i < num_taps; i++) {
            float_vec in[rate + 1];
            int tap = tap_locs_array[i];
            int load_idx = rate * vec_lanes + tap;

            if (load_idx % vec_lanes) {
                // Instead of doing an unaligned load, do aligned loads and
                // shuffle. This was originally to avoid partial overlap with a
                // vector in the store buffer, but it's basically always worth
                // doing this, because with rate > 1 this gets better sharing of
                // loaded values across multiple fmas. It's even more true if
                // the tap_locs occur in tight clusters, which they do for
                // TIIRs.
                int align_up = (load_idx + vec_lanes - 1) / vec_lanes * vec_lanes;
                int residual = align_up - load_idx;
                buffer.load(-align_up, rate + 1, in);
                for (int r = 0; r < rate; r++) {
                    in[r] = extract_slice(in[r], in[r + 1], residual);
                }
                // If we're getting the input via a shuffle, we should use the
                // load-and-broadcast-scalar form of the fma instruction.
                for (int r = 0; r < rate; r++) {
                    sum[r] += in[r] * load_coefficient(coeffs, i);
                }

            } else {
                buffer.load(-load_idx, rate, in);
                for (int r = 0; r < rate; r++) {
                    sum[r] += in[r] * coeffs[i];
                }
            }
        }

        for (int r = 0; r < rate; r++) {
            output[r] = sum[r];
        }
    }

    void reset() {
        buffer.reset();
    }

    SparseFIR(const std::vector<float> &taps) {
        assert((int)(taps.size()) == num_taps);
        std::memcpy(coeffs, &taps[0], num_taps * sizeof(float));
    }
};

#endif
