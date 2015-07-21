#include <android/log.h>
#include <jni.h>
#include <iostream>
#include <iomanip>
#include "avg_filter_uint32t.h"
#include "avg_filter_uint32t_arm.h"
#include "avg_filter_float.h"
#include "avg_filter_float_arm.h"
#include <sstream>

#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO, "oglc_run", __VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, "oglc_run", __VA_ARGS__)

extern "C" int halide_copy_to_host(void *, buffer_t *);
extern "C" int halide_device_sync(void *, buffer_t *);

typedef int (*filter_t) (buffer_t *, buffer_t *);

struct timing {
    filter_t filter;
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

template<class T> class Tester;

template<class T> bool doBlur(Tester<T> *tester, buffer_t bt_input, buffer_t bt_output, buffer_t bt_output_arm) {
    return false; // This abstract implementation should never be called
}

template<class T> bool doCopy(Tester<T> *tester, buffer_t bt_input, buffer_t bt_output, buffer_t bt_output_arm) {
    return false; // This abstract implementation should never be called
}

template<class T> class Tester {
    int debug_level;
  public:
    Tester(int _debug_level = 0): debug_level(_debug_level) {}

  private:

    bool validate(buffer_t actual, buffer_t expected) {
        int count_mismatches = 0;
        for (int i = 0; i < actual.extent[0]; i++) {
            for (int j = 0; j < actual.extent[1]; j++) {
                for (int k = 0; k < actual.extent[2]; k++) {
                    T actual_value =
                        ((T*)actual.host)[i * actual.stride[0] +
                                          j * actual.stride[1] +
                                          k * actual.stride[2]];
                    T expected_value =
                        ((T*)expected.host)[i * expected.stride[0] +
                                            j * expected.stride[1] +
                                            k * expected.stride[2]];
                    const float EPSILON = 0.00001f;
                    if (abs((double((actual_value - expected_value)) > EPSILON))) {
                        if (count_mismatches < 100) {
                            std::ostringstream str;
                            str << "actual and expected results differ at "
                                << "(" << i << ", " << j << ", " << k << "):"
                                << +actual_value << " != " << +expected_value
                                << "\n";
                            LOGI("%s", str.str().c_str());
                        }
                        count_mismatches++;
                    }
                }
            }
        }

        return count_mismatches == 0;
    }

    buffer_t make_interleaved_image(int width, int height, int channels, T host[]) {
        buffer_t bt_input = { 0 };
        bt_input.host = (uint8_t*)&host[0];
        bt_input.stride[0] = channels;
        bt_input.extent[0] = width;
        bt_input.stride[1] = channels * width;
        bt_input.extent[1] = height;
        bt_input.stride[2] = 1;
        bt_input.extent[2] = channels;
        bt_input.elem_size = sizeof(T);
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
                    oss << +((T*)bt.host)[i * bt.stride[0] +
                                          j * bt.stride[1] +
                                          k * bt.stride[2]];
                }
                oss << "]";
            }
            LOGI("%s", oss.str().c_str());
        }
    }

  public:
    bool test(buffer_t bt_input, buffer_t bt_output, buffer_t bt_output_arm,
        filter_t avg_filter, filter_t avg_filter_arm) {

        // Performance check
        bt_input.host_dirty = true;
        timing openglcompute(avg_filter, &bt_input, &bt_output);
        bt_input.host_dirty = true;
        timing openglcompute_with_copying(avg_filter, &bt_input, &bt_output);
        bt_input.host_dirty = true;
        timing arm(avg_filter_arm, &bt_input, &bt_output_arm);

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

        // Data correctness check
        bt_input.host_dirty = true;
        avg_filter(&bt_input, &bt_output);
        LOGI("Filter is done.");
        halide_device_sync(NULL, &bt_output);
        LOGI("Sync is done");
        halide_copy_to_host(NULL, &bt_output);

        LOGI("Output arm:");
        print(bt_output_arm);
        LOGI("Output openglcompute:");
        print(bt_output);

        bool matches = validate(bt_output, bt_output_arm);
        LOGI(matches? "Test passed.\n": "Test failed.\n");
        return matches;
    }

    void runTest() {
        int width = 4096;
        int height = 2048;
        int channels = 4;

        T *input = (T*)malloc(width * height * channels * sizeof(T));
        T *output = (T*)malloc(width * height * channels * sizeof(T));
        T *output_arm = (T*)malloc(width * height * channels * sizeof(T));
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
        bt_input.host_dirty = true;

        LOGI("Input :\n");
        print(bt_input);

        buffer_t bt_output = make_interleaved_image(width, height, channels, output);
        buffer_t bt_output_arm = make_interleaved_image(width, height, channels, output_arm);

        doBlur(this, bt_input, bt_output, bt_output_arm);
    }
};

template<> bool doBlur<float>(Tester<float> *tester, buffer_t bt_input, buffer_t bt_output, buffer_t bt_output_arm) {
    return tester->test(bt_input,
                        bt_output, bt_output_arm,
                        avg_filter_float,
                        avg_filter_float_arm);
}

template<> bool doBlur<uint32_t>(Tester<uint32_t> *tester, buffer_t bt_input, buffer_t bt_output, buffer_t bt_output_arm) {
    return tester->test(bt_input,
                        bt_output, bt_output_arm,
                        avg_filter_uint32t,
                        avg_filter_uint32t_arm);
}

int main(int argc, char** argv) {
    LOGI("\nvvvv vvvv vvvv");
    LOGI("\nTesting uint32_t...\n");
    (new Tester<uint32_t>())->runTest();
    LOGI("---- ---- ----");
    LOGI("\nTesting float...\n");
    (new Tester<float>())->runTest();
    LOGI("^^^^ ^^^^ ^^^^\n");
}

extern "C" {
JNIEXPORT void JNICALL Java_com_example_hellohalideopenglcompute_HalideOpenGLComputeActivity_runTest(JNIEnv *env, jobject obj) {
    main(0, NULL);
}
}
