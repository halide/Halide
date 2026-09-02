#ifndef SUPERBLURS_IIR_H
#define SUPERBLURS_IIR_H

// A first-order IIR filter with a single tap on the input, with coefficient
// one. Cascade it with an FIR to get multiple taps on the input.
template<int _stride>
struct alignas(64) IIR1 {
    constexpr static int stride = _stride;

    constexpr static int rate = 1;
    constexpr static int fmas_per_output = 1;  // Assume it can be vectorized perfectly

    const float coeff = 0.f;
    float_vec prev_out = {}, prev_in = {};

    void shape(size_t input_size, size_t *output_size) const {
        // Assume this is coupled with a tail-cancelling sparse FIR
        *output_size = input_size - stride;
    }

    void run(const float_vec *__restrict__ input, float_vec *__restrict__ output) {
        float_vec v = *input;

        if (stride <= 8) {
            prev_in = v;
            float c = coeff;
#pragma unroll
            for (int off : {1, 2, 4, 8}) {
                if (off < stride) continue;
                float_vec p = extract_slice(prev_in, v, vec_lanes - off);
                v += p * c;
                c *= c;
            }
            v += c * prev_out;
            *output = prev_out = v;
        } else if (stride == 16) {
            *output = prev_out = v + coeff * prev_out;
        } else {
            assert(false);
        }
    }

    void reset() {
        prev_out = float_vec{};
        prev_in = float_vec{};
    }

    IIR1(float c)
        : coeff(c) {
    }
};

template<int _stride>
struct alignas(64) IIR2Serial {
    constexpr static int stride = _stride;

    static_assert(stride >= vec_lanes && stride % vec_lanes == 0);

    constexpr static int rate = stride / vec_lanes;
    constexpr static int fmas_per_output = 2;  // Assume it can be vectorized perfectly

    const float alpha = 0.f;
    const float beta = 0.f;
    float_vec state[2 * rate] = {};

    void shape(size_t input_size, size_t *output_size) const {
        // We assume that all IIRs are actually TIIRs paired with an FIR, which
        // produces a couple of zeros at the end, so this is the offset that
        // makes the shape end up the right size.
        *output_size = input_size - 2 * stride;
    }

    void run(const float_vec *__restrict__ input, float_vec *__restrict__ output) {

#pragma unroll
        for (int i = 0; i < rate; i++) {
            auto v = input[i];
            v += alpha * state[2 * i];
            v += beta * state[2 * i + 1];
            state[2 * i + 1] = state[2 * i];
            output[i] = state[2 * i] = v;
        }
    }

    void reset() {
        memset(state, 0, sizeof(state));
    }

    IIR2Serial(float c0, float c1)
        : alpha(c0), beta(c1) {
    }
};

template<int _stride>
struct alignas(64) IIR2Pairwise {
    constexpr static int stride = _stride;

    constexpr static int rate = 2;
    constexpr static int fmas_per_output = 2;  // Assume it can be vectorized perfectly

    float_vec_t<stride> state[2] = {};
    const float c[5] = {};

    void shape(size_t input_size, size_t *output_size) const {
        *output_size = input_size - 2 * stride;
    }

    void run(const float_vec *__restrict__ input, float_vec *__restrict__ output) {
        const auto *in = (const float_vec_t<stride> *)input;
        auto *out = (float_vec_t<stride> *)output;

        /* A second-order IIR with input x and output y, can be represented like
           this (I believe this is known as controller-canonical form):

          s[i] = [0 1] s[i-1] + [0] x[i]
                 [b a]          [1]

          y[i] = [0 1] s[i]

          s[i] is a 2-vector representing the previous two outputs (oldest
          first). In the definition of s[i], the second row is just the
          definition of the IIR (using a and b for alpha and beta), and the
          first row just shuffles the old most-recent output to be the new
          second-most-recent output.

          We can also take two steps at a time. You can just subtitute out
          s[i-1] above and do the matrix math, but I find it easier to think
          about it starting from the recurrence;

          y[i] = a y[i-1] + b y[i-2] + x[i]

          The next output is given by:

          y[i+1] = a y[i] + b y[i-1] + x[i+1]

          Eliminating y[i] in the second line gives

          y[i+1] = a (a y[i-1] + b y[i-2] + x[i]) + b y[i-1] + x[i+1]

          Expanding and collecting like terms:

          y[i+1] = (a^2 + b) y[i-1] + ab y[i-2] + a x[i] + x[i+1]

          Represented as before using a state which is the previous two outputs
          (oldest first) this is equivalent to:

          s[i] = [b  a    ] s[i-2] + [1 0] x[i]
                 [ab a^2+b]        + [a 1] x[i+1]

          y[i]   = [1 0] s[i]
          y[i+1]   [0 1]

          This is more stable to accumulated rounding errors than the original,
          because y[i+1] looks back two outputs instead of just one, so the
          critical path length is shorter, and there have been fewer
          opportunities for rounding. But we can make it even more stable with a
          change of basis.

          Define our new state s' as follows:

          s' = [ 1 0] s
               [-1 1]

          Inverting, we get

          s = [1 0] s'
              [1 1]

          So now instead of being the last two outputs, s' represents two
          outputs ago, and the difference of the previous two outputs. For
          low-pass filters, these differences tend to be very very small, so it
          really helps precision to track that tiny number separately in its own
          floating point variable.

          If we replace s with s' in the equations above and multiply out the
          matrices, we get:

          s'[i] = [b        a      ] s'[i-2] + [1   0] x[i]
                  [ab+a^2-a a^2+b-a]           [a-1 1] x[i+1]

          y[i]   = [1 0] s'[i]
          y[i+1]   [1 1]

          This is what is implemented below. c[0] through c[4] are the
          non-trivial coefficients in the matrix above in this order:

          s'[i] = [c[0] c[1]] s'[i-2] + [1    0] x[i]
                  [c[2] c[3]]           [c[4] 1] x[i+1]


          Note that we're now doing 5 fmas and an add to produce two outputs, as
          opposed to the serial method above, which would require 4 fmas to
          produce two outputs. However, in practice, the performance is not
          measurably worse, because eliminating the dependence of y[i+1] on y[i]
          eases up instruction latency issues, which are the limiting factor in
          the serial version.
        */

        for (int i = 0; i < rate * vec_lanes / stride; i += 2) {
            auto v0 = in[i], v1 = in[i + 1];
            auto s0 = state[0], s1 = state[1];

            auto new_s0 = v0;
            new_s0 += c[0] * s0;
            new_s0 += c[1] * s1;
            auto new_s1 = v1;
            new_s1 += c[2] * s0;
            new_s1 += c[3] * s1;
            new_s1 += c[4] * v0;

            s0 = new_s0;
            s1 = new_s1;

            out[i] = s0;
            out[i + 1] = s0 + s1;

            state[0] = s0;
            state[1] = s1;
        }
    }

    void reset() {
        memset(state, 0, sizeof(state));
    }

    IIR2Pairwise(double alpha, double beta)
        : c{float(alpha + beta),
            float(alpha),
            float(alpha * (beta + alpha - 1)),
            float(alpha * (alpha - 1) + beta),
            float(alpha - 1)} {
    }
};

// This is essentially the same trick as IIR2Pairwise, with the same change of
// basis, but unrolled 16x instead of 2x so that we can do the matrix multiply
// of x using a full vector for each column. It's plausibly good for very
// low-stride IIRs, but in practice we find that using the dilation trick below
// is better.
template<int _stride>
struct alignas(64) IIR2Blockwise {

    constexpr static int stride = _stride;

    constexpr static int rate = stride;
    constexpr static int fmas_per_output = 2;  // Assume it can be vectorized perfectly

    // Vector constants
    float_vec reverse_impulse_response[stride] = {};
    float_vec basis1 = {}, basis2 = {};
    float_vec mat[vec_lanes];

    // State
    float_vec_t<stride> diff = {};
    float_vec_t<stride> prev_out = {};

    // Scalar constants
    const float alpha = 0.f;
    const float beta = 0.f;
    float diff_b1 = 0.f, diff_b2 = 0.f;

    void run(const float_vec *__restrict__ input, float_vec *__restrict__ output) {
        constexpr int num_accumulators = 2;

        static_assert(rate == stride);
        float_vec out[num_accumulators][stride] = {};

        // Within-block portion
#pragma unroll
        for (int k = 0; k < vec_lanes; k += num_accumulators) {
#pragma unroll
            for (int c = 0; c < num_accumulators; c++) {
#pragma unroll
                for (int s = 0; s < stride; s++) {
                    out[c][s] += ((const float *)input)[(k + c) * stride + s] *
                                 mat[k + c];
                }
            }
        }

        for (int s = 0; s < stride; s++) {
            for (int c = 1; c < num_accumulators; c++) {
                out[0][s] += out[c][s];
            }
        }

        float_vec_t<stride> next_diff;

        float_vec next_diff_sum = {};
        float_vec zero = {};
        for (int s = 0; s < stride; s++) {
            auto v = extract_slice(s == 0 ? zero : input[s - 1], input[s], 16 - stride) - input[s];
            next_diff_sum += v * reverse_impulse_response[s];
        }

        if (stride == 1) {
            next_diff = horizontal_sum_reduce(next_diff_sum);
        } else if (stride <= 16) {
            next_diff = incomplete_horizontal_sum_reduce<stride>(next_diff_sum);
        } else {
            printf("Bad stride: %d\n", stride);
            assert(false);
        }
        next_diff += diff * diff_b1;
        next_diff += prev_out * diff_b2;

        for (int s = 0; s < stride; s++) {
            float p0;
            if constexpr (stride == 1) {
                p0 = prev_out;
            } else {
                p0 = prev_out[s];
            }

            float_vec o = out[0][s];

            o += p0 * basis2;

            if constexpr (stride == 1) {
                o += diff * basis1;
            } else {
                o += diff[s] * basis1;
            }

            out[0][s] = o;
        }

        interleave_vectors<stride>(out[0], output);

        // The next prev_out is the last stride lanes of the last output vector
        std::memcpy(&prev_out, ((const float *)output) + stride * (vec_lanes - 1), sizeof(prev_out));
        diff = next_diff;
    }

    void reset() {
        diff = float_vec{};
        prev_out = float_vec{};
    }

    IIR2Blockwise(float c0, float c1)
        : alpha(c0), beta(c1) {

        double impulse_response_mem[vec_lanes * 2 + 1] = {};
        double *impulse_response_double = impulse_response_mem + 1;

        impulse_response_double[0] = 1;
        for (int i = 1; i < 2 * vec_lanes; i++) {
            impulse_response_double[i] = (alpha * impulse_response_double[i - 1] +
                                          beta * impulse_response_double[i - 2]);
        }

        float_vec impulse_response;
        for (int i = 0; i < 16; i++) {
            impulse_response[i] = impulse_response_double[i];
        }
        for (int i = 0; i < 16; i++) {
            for (int s = 0; s < stride; s++) {
                ((float *)reverse_impulse_response)[stride * i + s] = impulse_response[15 - i];
            }
        }

        double diff_b1_double, diff_b2_double;
        for (int i = 0; i < 16; i++) {
            double b1 = impulse_response_double[i] * beta;
            double b2 = b1 + impulse_response_double[i + 1];
            basis1[i] = b1;
            basis2[i] = b2;
            if (i == 14) {
                diff_b1_double = b1;
                diff_b2_double = b2;
            } else if (i == 15) {
                diff_b1 = diff_b1_double - b1;
                diff_b2 = diff_b2_double - b2;
            }
        }

        float_vec zero = {};
        for (int k = 0; k < vec_lanes; k++) {
            mat[k] = extract_slice(zero, impulse_response, 16 - k);
        }
    }
};

template<int stride, bool stabilized = false>
auto IIR2(double alpha, double beta) {
    if constexpr (stride >= vec_lanes) {
        if constexpr (stabilized && stride == vec_lanes) {
            return IIR2Pairwise<vec_lanes>(alpha, beta);
        } else {
            return IIR2Serial<stride>(alpha, beta);
        }
    } else {
        return Cascade(
            DenseFIR<3, stride, 4, true, 1, true>({1.f, (float)alpha, (float)(-beta)}),
            IIR2<stride * 2, stabilized>(alpha * alpha + 2 * beta, -beta * beta));
    }
}

#endif
