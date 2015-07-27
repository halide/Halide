#include <android/log.h>
#include <jni.h>
#include <iostream>
#include <iomanip>
#include "two_kernels_filter.h"
#include <sstream>

#include "HalideRuntimeOpenGLCompute.h"

#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO, "oglc_run", __VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, "oglc_run", __VA_ARGS__)

extern "C" int halide_copy_to_host(void *, buffer_t *);
extern "C" int halide_device_sync(void *, buffer_t *);
extern "C" int halide_device_free(void *, buffer_t* buf);
extern "C" void halide_device_release(void *, const halide_device_interface *interface);

buffer_t make_interleaved_image(int width, int height, int channels, int32_t host[]) {
    buffer_t bt_input = { 0 };
    bt_input.host = (uint8_t*)&host[0];
    bt_input.stride[0] = channels;
    bt_input.extent[0] = width;
    bt_input.stride[1] = channels * width;
    bt_input.extent[1] = height;
    bt_input.stride[2] = 1;
    bt_input.extent[2] = channels;
    bt_input.elem_size = sizeof(int32_t);
    return bt_input;
}

void print(buffer_t bt) {
    for (int j = 0; j < std::min(bt.extent[1], 10); j++) {
        std::stringstream oss;
        for (int i = 0; i < std::min(bt.extent[0], 10); i++) {
            oss << " [";
            for (int k = 0; k < bt.extent[2]; k++) {
                oss << std::fixed << std::setprecision(1);
                if (k > 0) {
                    oss << std::setw(4);
                }
                oss << +((int32_t*)bt.host)[i * bt.stride[0] +
                                      j * bt.stride[1] +
                                      k * bt.stride[2]];
            }
            oss << "]";
        }
        LOGI("%s", oss.str().c_str());
    }
}

int main(int argc, char** argv) {
    LOGI("\nvvvv vvvv vvvv");

    int width = 128;
    int height = 128;
    int channels = 4;

    int32_t *input = (int32_t*)malloc(width * height * channels * sizeof(int32_t));
    int32_t *output = (int32_t*)malloc(width * height * channels * sizeof(int32_t));
    LOGI("Allocated memory for %dx%dx%d image", width, height, channels);

    buffer_t bt_input = make_interleaved_image(width, height, channels, input);
    for (int i = 0; i < std::min(bt_input.extent[0], width); i++) {
        for (int j = 0; j < std::min(bt_input.extent[1], height); j++) {
            for (int k = 0; k < bt_input.extent[2]; k++) {
                input[i * bt_input.stride[0] +
                      j * bt_input.stride[1] +
                      k * bt_input.stride[2]] = ((i + j) % 2) * 6;
            }
        }
    }
    LOGI("Input :\n");
    print(bt_input);
    bt_input.host_dirty = true;

    buffer_t bt_output = make_interleaved_image(width, height, channels, output);

    two_kernels_filter(&bt_input, &bt_output);
    LOGI("Filter is done.");
    halide_device_sync(NULL, &bt_output);
    LOGI("Sync is done");
    halide_copy_to_host(NULL, &bt_output);

    LOGI("Output :\n");
    print(bt_output);

    int count_mismatches = 0;
    for (int i = 0; i < bt_output.extent[0]; i++) {
        for (int j = 0; j < bt_output.extent[1]; j++) {
            for (int k = 0; k < bt_output.extent[2]; k++) {
                int32_t output_value =
                    ((int32_t*)bt_output.host)[i * bt_output.stride[0] +
                                               j * bt_output.stride[1] +
                                               k * bt_output.stride[2]];
                int32_t input_value =
                    ((int32_t*)bt_input.host)[i * bt_input.stride[0] +
                                              j * bt_input.stride[1] +
                                              k * bt_input.stride[2]];
                if (output_value != input_value) {
                    if (count_mismatches < 100) {
                        std::ostringstream str;
                        str << "bt_output and bt_input results differ at "
                            << "(" << i << ", " << j << ", " << k << "):"
                            << output_value << " != " << input_value
                            << "\n";
                        LOGI("%s", str.str().c_str());
                    }
                    count_mismatches++;
                }
            }
        }
    }

    LOGI(count_mismatches == 0? "Test passed.\n": "Test failed.\n");

    halide_device_free(NULL, &bt_input);
    halide_device_free(NULL, &bt_output);

    halide_device_release(NULL, halide_openglcompute_device_interface());

    LOGI("^^^^ ^^^^ ^^^^\n");
}

extern "C" {
JNIEXPORT void JNICALL Java_com_example_hellohalideopenglcompute_HalideOpenGLComputeActivity_runTwoKernelsTest(JNIEnv *env, jobject obj) {
    main(0, NULL);
}
}
