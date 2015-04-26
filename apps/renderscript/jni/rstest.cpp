#include "generated_blur_rs.h"
#include "generated_blur_arm.h"
#include "generated_blur_vectorized_rs.h"
#include "generated_blur_vectorized_arm.h"

#include "generated_copy_rs.h"
#include "generated_copy_arm.h"
#include "generated_copy_vectorized_rs.h"
#include "generated_copy_vectorized_arm.h"

#include <iostream>

extern "C" int halide_copy_to_host(void *, buffer_t *);

bool validate(buffer_t actual, buffer_t expected) {
    int count_mismatches = 0;
    for (int i = 0; i < actual.extent[0]; i++) {
        for (int j = 0; j < actual.extent[1]; j++) {
            for (int k = 0; k < actual.extent[2]; k++) {
                uint8_t actual_value =
                    actual.host[i * actual.stride[0] + j * actual.stride[1] +
                                k * actual.stride[2]];
                uint8_t expected_value =
                    expected
                        .host[i * expected.stride[0] + j * expected.stride[1] +
                              k * expected.stride[2]];
                if (actual_value != expected_value) {
                    if (count_mismatches < 100) {
                        char buf[512];
                        sprintf(buf, "actual and expected results differ at "
                                     "(%d, %d, %d): %d != %d\n",
                                i, j, k, actual_value, expected_value);
                        std::cout << buf;
                    }
                    count_mismatches++;
                }
            }
        }
    }

    std::cout << "---===---===---===---" << std::endl;
    std::cout << "RS(ARM):" << std::endl;

    for (int j = 0; j < std::min(actual.extent[1], 10); j++) {
        for (int i = 0; i < std::min(actual.extent[0], 10); i++) {
            std::cout << " [";
            for (int k = 0; k < actual.extent[2]; k++) {
                std::cout.width(2);

                char buffer[33];
                uint8_t actual_value =
                    actual.host[i * actual.stride[0] + j * actual.stride[1] +
                                k * actual.stride[2]];
                uint8_t expected_value =
                    expected
                        .host[i * expected.stride[0] + j * expected.stride[1] +
                              k * expected.stride[2]];
                if (actual_value != expected_value) {
                    sprintf(buffer, "%2d(%2d)", actual_value, expected_value);
                } else {
                    sprintf(buffer, "%2d", actual_value);
                }
                std::cout << buffer;
            }
            std::cout << "]";
        }

        std::cout << std::endl;
    }

    return count_mismatches == 0;
}

buffer_t make_planar_image(int width, int height, int channels,
                           uint8_t host[]) {
    buffer_t bt_input = { 0 };
    bt_input.host = &host[0];
    bt_input.host_dirty = true;
    bt_input.stride[0] = 1;
    bt_input.extent[0] = width;
    bt_input.stride[1] = width;
    bt_input.extent[1] = height;
    bt_input.stride[2] = width * height;
    bt_input.extent[2] = channels;
    bt_input.elem_size = 1;
    return bt_input;
}

buffer_t make_interleaved_image(int width, int height, int channels,
                                uint8_t host[]) {
    buffer_t bt_input = { 0 };
    bt_input.host = &host[0];
    bt_input.host_dirty = true;
    bt_input.stride[0] = 4;
    bt_input.extent[0] = width;
    bt_input.stride[1] = 4 * width;
    bt_input.extent[1] = height;
    bt_input.stride[2] = 1;
    bt_input.extent[2] = channels;
    bt_input.elem_size = 1;
    return bt_input;
}

typedef int  (filter_t) (buffer_t *, buffer_t *);

struct timing {
    filter_t *filter;
    buffer_t *bt_input;
    buffer_t *bt_output;
    double worst_t = 0;
    int worst_rep = 0;
    double best_t = DBL_MAX;
    int best_rep = 0;

    timing(filter_t _filter, buffer_t *_bt_input, buffer_t *_bt_output):
        filter(_filter), bt_input(_bt_input), bt_output(_bt_output) {}

    int run(int n_reps) {
        timeval t1, t2;
        for (int i = 0; i < n_reps; i++) {
            gettimeofday(&t1, NULL);
            int error = filter(bt_input, bt_output);
            gettimeofday(&t2, NULL);
            if (error) {
                return(error);
            }
            double t = (t2.tv_sec - t1.tv_sec) * 1000.0 + (t2.tv_usec - t1.tv_usec) / 1000.0;
            if (t < best_t) {
                best_t = t;
                best_rep = i;
            }
            if (t > worst_t) {
                worst_t = t;
                worst_rep = i;
            }
        }
        return 0;
    }
};

bool test(buffer_t bt_input, buffer_t bt_output, buffer_t bt_output_arm,
          filter_t generated_rs, filter_t generated_arm) {
    for (int j = 0; j < std::min(bt_input.extent[1], 10); j++) {
        for (int i = 0; i < std::min(bt_input.extent[0], 10); i++) {
            std::cout << " [";
            for (int k = 0; k < bt_input.extent[2]; k++) {
                std::cout.width(2);

                char buffer[33];
                sprintf(buffer, "%d", bt_input.host[i * bt_input.stride[0] +
                                                    j * bt_input.stride[1] +
                                                    k * bt_input.stride[2]]);
                std::cout << buffer;
            }
            std::cout << "]";
        }

        std::cout << std::endl;
    }
    const int n_reps = 500;
    timing rs(generated_rs, &bt_input, &bt_output);
    int error;
    if (error = rs.run(n_reps) != 0) {
        std::cout << "Halide returned error: " << error << std::endl;
    }
    if (bt_output.dev) {
        halide_copy_to_host(NULL, &bt_output);
    }

    timing arm(generated_arm, &bt_input, &bt_output_arm);
    if (error = arm.run(n_reps) != 0) {
        std::cout << "Halide returned error: " << error << std::endl;
    }

    printf("Out of %d runs best times are:\nRS:  %fms(@%d) \nARM: %fms(@%d)\n", n_reps,
           rs.best_t, rs.best_rep, arm.best_t, arm.best_rep);
    printf("Out of %d runs worst times are:\nRS:  %fms(@%d)\nARM: %fms(@%d)\n", n_reps,
           rs.worst_t, rs.worst_rep, arm.worst_t, arm.worst_rep);

    return validate(bt_output, bt_output_arm);
}

int main(int argc, char **argv) {
    const int width = 256;
    const int height = 512;
    const int channels = 4;

    uint8_t input_image[width * height * channels];
    uint8_t output_image[width * height * channels];
    uint8_t output_image_arm[width * height * channels];

    buffer_t bt_input = make_planar_image(width, height, channels, input_image);
    const int channels_stride = 1;  // chunky image
    for (int i = 0; i < std::min(bt_input.extent[0], width); i++) {
        for (int j = 0; j < std::min(bt_input.extent[1], height); j++) {
            for (int k = 0; k < bt_input.extent[2]; k++) {
                input_image[i * bt_input.stride[0] + j * bt_input.stride[1] +
                            k * bt_input.stride[2]] = ((i + j) % 2) * 6;
            }
        }
    }
    buffer_t bt_output =
        make_planar_image(width, height, channels, output_image);
    buffer_t bt_output_arm =
        make_planar_image(width, height, channels, output_image_arm);

    std::cout << "Planar blur:\n";
    bool correct = true;
    if (!test(bt_input, bt_output, bt_output_arm, generated_blur_rs,
              generated_blur_arm)) {
        correct = false;
    }
    std::cout << "Planar copy:\n";
    if (!test(bt_input, bt_output, bt_output_arm, generated_copy_rs,
              generated_copy_arm)) {
        correct = false;
    }

    buffer_t bt_interleaved_input =
        make_interleaved_image(width, height, channels, input_image);
    for (int i = 0; i < std::min(bt_interleaved_input.extent[0], width); i++) {
        for (int j = 0; j < std::min(bt_interleaved_input.extent[1], height);
             j++) {
            for (int k = 0; k < bt_interleaved_input.extent[2]; k++) {
                input_image[i * bt_interleaved_input.stride[0] +
                            j * bt_interleaved_input.stride[1] +
                            k * bt_interleaved_input.stride[2]] =
                    ((i + j) % 2) * 6;
            }
        }
    }
    buffer_t bt_interleaved_output =
        make_interleaved_image(width, height, channels, output_image);
    buffer_t bt_interleaved_output_arm =
        make_interleaved_image(width, height, channels, output_image_arm);

    std::cout << "\nInterleaved(vectorized) blur:\n";
    if (!test(bt_interleaved_input, bt_interleaved_output,
              bt_interleaved_output_arm, generated_blur_vectorized_rs,
              generated_blur_vectorized_arm)) {
        correct = false;
    }
    std::cout << "\nInterleaved(vectorized) copy:\n";
    if (!test(bt_interleaved_input, bt_interleaved_output,
              bt_interleaved_output_arm, generated_copy_vectorized_rs,
              generated_copy_vectorized_arm)) {
        correct = false;
    }

    if (correct) {
        std::cout << "Done!" << std::endl;
    } else {
        std::cout << "Failed!" << std::endl;
    }
}