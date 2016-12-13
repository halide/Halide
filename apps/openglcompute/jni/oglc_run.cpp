#include <android/log.h>
#include <jni.h>
#include <iostream>
#include <iomanip>
#include "avg_filter_uint32t.h"
#include "avg_filter_uint32t_arm.h"
#include "avg_filter_float.h"
#include "avg_filter_float_arm.h"
#include <sstream>

#include "HalideBuffer.h"
#include "HalideRuntimeOpenGLCompute.h"

#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO, "oglc_run", __VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, "oglc_run", __VA_ARGS__)

typedef int (*filter_t) (buffer_t *, buffer_t *);

struct timing {
    filter_t filter;
    Halide::Buffer<> *input;
    Halide::Buffer<> *output;
    double worst_t = 0;
    int worst_rep = 0;
    double best_t = DBL_MAX;
    int best_rep = 0;

    template<typename T>
    timing(filter_t filter, Halide::Buffer<T> *input, Halide::Buffer<T> *output):
        filter(filter), input(&input->template as<void>()), output(&output->template as<void>()) {}

    int run(int n_reps, bool with_copying) {
        timeval t1, t2;
        for (int i = 0; i < n_reps; i++) {
            input->set_host_dirty();
            gettimeofday(&t1, NULL);
            int error = filter(*input, *output);
            output->device_sync();

            if (with_copying) {
                output->copy_to_host();
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

template<class T> bool doBlur(Tester<T> *tester,
                              Halide::Buffer<T> bt_input,
                              Halide::Buffer<T> bt_output,
                              Halide::Buffer<T> bt_output_arm) {
    return false; // This abstract implementation should never be called
}

template<class T> bool doCopy(Tester<T> *tester,
                              Halide::Buffer<T> bt_input,
                              Halide::Buffer<T> bt_output,
                              Halide::Buffer<T> bt_output_arm) {
    return false; // This abstract implementation should never be called
}

template<class T> class Tester {
    int debug_level;
  public:
    Tester(int _debug_level = 0): debug_level(_debug_level) {}

  private:

    bool validate(Halide::Buffer<T> actual, Halide::Buffer<T> expected) {
        int count_mismatches = 0;
        actual.for_each_element([&](int x, int y, int c) {
            T actual_value = actual(x, y, c);
            T expected_value = expected(x, y, c);
            const float EPSILON = 0.00001f;
            if (abs((double((actual_value - expected_value)) > EPSILON))) {
                if (count_mismatches < 100) {
                    std::ostringstream str;
                    str << "actual and expected results differ at "
                        << "(" << x << ", " << y << ", " << c << "):"
                        << +actual_value << " != " << +expected_value
                        << "\n";
                    LOGI("%s", str.str().c_str());
                }
                count_mismatches++;
            }
        });

        return count_mismatches == 0;
    }

    void print(Halide::Buffer<T> buf) {
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

  public:
    bool test(Halide::Buffer<T> input,
              Halide::Buffer<T> output,
              Halide::Buffer<T> output_arm,
              filter_t avg_filter,
              filter_t avg_filter_arm) {

        // Performance check
        input.set_host_dirty();
        timing openglcompute(avg_filter, &input, &output);
        input.set_host_dirty();
        timing openglcompute_with_copying(avg_filter, &input, &output);
        input.set_host_dirty();
        timing arm(avg_filter_arm, &input, &output_arm);

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
        input.set_host_dirty();
        avg_filter(input, output);
        LOGI("Filter is done.");
        output.device_sync();
        LOGI("Sync is done");
        output.copy_to_host();

        LOGI("Output arm:");
        print(output_arm);
        LOGI("Output openglcompute:");
        print(output);

        bool matches = validate(output, output_arm);
        LOGI(matches? "Test passed.\n": "Test failed.\n");

        return matches;
    }

    void runTest() {
        int width = 4096;
        int height = 2048;
        int channels = 4;

        auto input = Halide::Buffer<T>::make_interleaved(width, height, channels);
        LOGI("Allocated memory for %dx%dx%d image", width, height, channels);

        input.for_each_element([&](int i, int j, int k) {
            input(i, j, k) = ((i + j) % 2) * 6;
        });

        LOGI("Input :\n");
        print(input);

        auto output = Halide::Buffer<T>::make_interleaved(width, height, channels);
        auto output_arm = Halide::Buffer<T>::make_interleaved(width, height, channels);

        doBlur(this, input, output, output_arm);
    }
};

template<> bool doBlur<float>(Tester<float> *tester,
                              Halide::Buffer<float> bt_input,
                              Halide::Buffer<float> bt_output,
                              Halide::Buffer<float> bt_output_arm) {
    return tester->test(bt_input,
                        bt_output, bt_output_arm,
                        avg_filter_float,
                        avg_filter_float_arm);
}

template<> bool doBlur<uint32_t>(Tester<uint32_t> *tester,
                                 Halide::Buffer<uint32_t> bt_input,
                                 Halide::Buffer<uint32_t> bt_output,
                                 Halide::Buffer<uint32_t> bt_output_arm) {
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

    halide_device_release(NULL, halide_openglcompute_device_interface());

    LOGI("^^^^ ^^^^ ^^^^\n");
}

extern "C" {
JNIEXPORT void JNICALL Java_com_example_hellohalideopenglcompute_HalideOpenGLComputeActivity_runTest(JNIEnv *env, jobject obj) {
    main(0, NULL);
}
}
