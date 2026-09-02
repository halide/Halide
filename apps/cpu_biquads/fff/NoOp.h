#ifndef SUPERBLURS_NOOP_H
#define SUPERBLURS_NOOP_H

struct NoOp {
    static constexpr int rate = 1;
    static constexpr int fmas_per_output = 0;

    void shape(size_t input_size, size_t *output_size) const {
        *output_size = input_size;
    }

    void run(const float_vec *input, float_vec *output) {
        *output = *input;
    }

    void reset() {
    }
};

struct NoOp2D {
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
        assert(input_width == output_width);
        assert(input_height == output_height);
        if (mode == Assign) {
            for (int y = min_y; y < max_y; y++) {
                std::memcpy(output + y * output_stride + min_x,
                            input + y * input_stride + min_x,
                            (max_x - min_x) * sizeof(float));
            }
        } else {
            for (int y = min_y; y < max_y; y++) {
                for (int x = min_x; x < max_x; x++) {
                    // Assume autovectorization of this loop is easy
                    output[y * output_stride + x] += input[y * input_stride + x];
                }
            }
        }
    }

    void shape(size_t w, size_t h, size_t *ow, size_t *oh) const {
        *ow = w;
        *oh = h;
    }

    void reset() {
    }
};

#endif
