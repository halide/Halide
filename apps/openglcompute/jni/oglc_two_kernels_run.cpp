#include "two_kernels_filter.h"
#include <android/log.h>
#include <iomanip>
#include <iostream>
#include <jni.h>
#include <sstream>

#include "HalideBuffer.h"
#include "HalideRuntimeOpenGLCompute.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "oglc_run", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "oglc_run", __VA_ARGS__)

template<typename T>
void print(Halide::Runtime::Buffer<T, 3> buf) {
    for (int j = 0; j < std::min(buf.height(), 10); j++) {
        std::stringstream oss;
        for (int i = 0; i < std::min(buf.width(), 10); i++) {
            oss << " [";
            for (int k = 0; k < buf.channels(); k++) {
                oss << std::fixed << std::setprecision(1);
                if (k > 0) {
                    oss << std::setw(4);
                }
                oss << +buf(i, j, k);
            }
            oss << "]";
        }
        LOGI("%s", oss.str().c_str());
    }
}

int main(int argc, char **argv) {
    LOGI("\nvvvv vvvv vvvv");

    int width = 128;
    int height = 128;
    int channels = 4;

    auto input = Halide::Runtime::Buffer<int, 3>::make_interleaved(width, height, channels);
    LOGI("Allocated memory for %dx%dx%d image", width, height, channels);

    input.for_each_element([&](int i, int j, int k) {
        input(i, j, k) = ((i + j) % 2) * 6;
    });

    LOGI("Input :\n");
    print(input);

    auto output = Halide::Runtime::Buffer<int, 3>::make_interleaved(width, height, channels);

    two_kernels_filter(input, output);
    LOGI("Filter is done.");
    output.device_sync();
    LOGI("Sync is done");
    output.copy_to_host();

    LOGI("Output :\n");
    print(output);

    int count_mismatches = 0;
    output.for_each_element([&](int i, int j, int k) {
        int32_t output_value = output(i, j, k);
        int32_t input_value = input(i, j, k);
        if (output_value != input_value) {
            if (count_mismatches < 100) {
                std::ostringstream str;
                str << "output and input results differ at "
                    << "(" << i << ", " << j << ", " << k << "):"
                    << output_value << " != " << input_value
                    << "\n";
                LOGI("%s", str.str().c_str());
            }
            count_mismatches++;
        }
    });

    LOGI(count_mismatches == 0 ? "Test passed.\n" : "Test failed.\n");

    halide_device_release(NULL, halide_openglcompute_device_interface());

    LOGI("^^^^ ^^^^ ^^^^\n");
}

extern "C" {
JNIEXPORT void JNICALL Java_com_example_hellohalideopenglcompute_HalideOpenGLComputeActivity_runTwoKernelsTest(JNIEnv *env, jobject obj) {
    main(0, NULL);
}
}
