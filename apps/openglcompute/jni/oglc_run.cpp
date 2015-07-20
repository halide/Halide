#include <android/log.h>
#include <iostream>
#include <iomanip>
#include "avg_filter.h"
#include "avg_filter_arm.h"
#include <sstream>

const int CHANNELS = 4;

buffer_t make_image(int width, int depth, float host[]) {
    buffer_t bt_input = { 0 };
    bt_input.host = (uint8_t*)&host[0];
    bt_input.stride[0] = depth;
    bt_input.extent[0] = width;
    bt_input.stride[1] = 1;
    bt_input.extent[1] = depth;
    bt_input.elem_size = sizeof(float);
    return bt_input;
}

#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO, "oglc_run", __VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, "oglc_run", __VA_ARGS__)

void print(buffer_t bt) {
    for (int i = 0; i < std::min(bt.extent[0], 1024*1024*1024); i+=256) {
        std::stringstream oss;
        oss << "@" << i << " [";
        for (int j = 0; j < std::min(bt.extent[1], 100); j++) {
            oss << std::fixed << std::setprecision(1);
            oss << std::setw(8);
            oss << +((float*)bt.host)[i * bt.stride[0] + j * bt.stride[1]];
        }
        oss << " ]";
        LOGI("%s", oss.str().c_str());
    }
}

extern "C" int halide_copy_to_host(void *, buffer_t *);
extern "C" int halide_device_sync(void *, buffer_t *);

typedef int (filter_t) (buffer_t *, buffer_t *);

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

    int run(int n_reps, bool with_copying) {
        timeval t1, t2;
        for (int i = 0; i < n_reps; i++) {
            bt_input->host_dirty = true;
            gettimeofday(&t1, NULL);
            int error = filter(bt_input, bt_output);
            halide_device_sync(NULL, bt_output);

            if (with_copying) {
                if (bt_output->dev) {
                    halide_copy_to_host(NULL, bt_output);
                }
            }
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

float avg(buffer_t bt, int i, int j) {
    float *floats = (float*)bt.host;
    return (floats[std::max(i - 1, 0) * bt.stride[0] + j * bt.stride[1]]
            + floats[i * bt.stride[0] + j * bt.stride[1]]
            + floats[std::min(i + 1, bt.extent[0] - 1) * bt.stride[0] + j * bt.stride[1]]) / 3.0f;
}

int main(int argc, char** argv) {
    int width = 63*1024;
    int depth = CHANNELS;
    float *input = (float*)malloc(sizeof(float) * width * depth);
    float *output = (float*)malloc(sizeof(float) * width * depth);

    buffer_t bt_input = make_image(width, depth, input);
    for (int i = 0; i < std::min(bt_input.extent[0], width); i++) {
        for (int j = 0; j < std::min(bt_input.extent[1], depth); j++) {
            input[i * bt_input.stride[0] +
                  j * bt_input.stride[1]] = ((i + j) % 2) * 6;
        }
    }

    LOGI("Input :\n");
    print(bt_input);

    buffer_t bt_output = make_image(width, depth, output);

    // Performance check

    timing openglcompute(avg_filter, &bt_input, &bt_output);
    timing openglcompute_with_copying(avg_filter, &bt_input, &bt_output);
    timing arm(avg_filter_arm, &bt_input, &bt_output);

    const int N_REPS = 10;
    arm.run(N_REPS, false);
    openglcompute.run(N_REPS, false);
    openglcompute_with_copying.run(N_REPS, true);

    LOGI("Out of %d runs best times are:\n"
            "openglcompute:            %fms(@%d)\n"
            "openglcompute(with copy): %fms(@%d)\n"
            "ARM:                      %fms(@%d)\n",
            N_REPS,
            openglcompute.best_t, openglcompute.best_rep,
            openglcompute_with_copying.best_t, openglcompute_with_copying.best_rep,
            arm.best_t, arm.best_rep);
    LOGI("Out of %d runs worst times are:\n"
            "openglcompute:            %fms(@%d)\n"
            "openglcompute(with copy): %fms(@%d)\n"
            "ARM:                      %fms(@%d)\n",
            N_REPS,
            openglcompute.worst_t, openglcompute.worst_rep,
            openglcompute_with_copying.worst_t, openglcompute_with_copying.worst_rep,
            arm.worst_t, arm.worst_rep);

    // Data check

    bt_input.host_dirty = true;
    avg_filter(&bt_input, &bt_output);
    LOGI("Filter is done.");
    halide_device_sync(NULL, &bt_output);
    LOGI("Sync is done");
    halide_copy_to_host(NULL, &bt_output);

    LOGI("Output :");
    print(bt_output);

    const int MAX_REPORTED_MISMATCHES = 10;
    const float EPS = 0.00001;
    int mismatches = 0;
    for (int i = 0; i < std::min(bt_input.extent[0], width) && mismatches < MAX_REPORTED_MISMATCHES; i++) {
        for (int j = 0; j < std::min(bt_input.extent[1], depth) && mismatches < MAX_REPORTED_MISMATCHES; j++) {
            float avg_value = avg(bt_input, i, j);
            float output_value = output[i * bt_input.stride[0] + j * bt_input.stride[1]];
            if (fabs(avg_value - output_value) > EPS) {
                LOGI("Mismatch at (%d, %d): expected:%f, actual:%f", i, j, avg_value, output_value);
                if (++mismatches >= MAX_REPORTED_MISMATCHES) {
                    LOGI("too many. There could be more, but we are bailing out.");
                }
            }
        }
    }

    LOGI(mismatches? "Test failed.\n": "Test passed.\n");
}
