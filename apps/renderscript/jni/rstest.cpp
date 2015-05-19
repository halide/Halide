#include <android/log.h>
#include <jni.h>
#include "generated_blur_rs_float.h"
#include "generated_blur_arm_float.h"

#include "generated_copy_rs_float.h"
#include "generated_copy_arm_float.h"

#include "generated_blur_rs_uint8.h"
#include "generated_blur_arm_uint8.h"

#include "generated_copy_rs_uint8.h"
#include "generated_copy_arm_uint8.h"


#include <iostream>
#include <sys/system_properties.h>
#include <sstream>
#include <iomanip>

#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO, "rstest", __VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, "rstest", __VA_ARGS__)


extern "C" int halide_copy_to_host(void *, buffer_t *);
extern "C" int halide_device_sync(void *, buffer_t *);
extern "C" void halide_set_renderscript_cache_dir(const char *c);

typedef int (filter_t) (buffer_t *, buffer_t *);

struct timing {
    filter_t *filter;
    const char* cacheDir;
    buffer_t *bt_input;
    buffer_t *bt_output;
    double worst_t = 0;
    int worst_rep = 0;
    double best_t = DBL_MAX;
    int best_rep = 0;

    timing(filter_t _filter, const char* _cacheDir, buffer_t *_bt_input, buffer_t *_bt_output):
        filter(_filter), cacheDir(_cacheDir), bt_input(_bt_input), bt_output(_bt_output) {}

    int run(int n_reps, bool with_copying) {
        timeval t1, t2;
        halide_set_renderscript_cache_dir(cacheDir);
        for (int i = 0; i < n_reps; i++) {
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

template<class T> bool doBlur(Tester<T> *tester, const char *cacheDir, buffer_t bt_input, buffer_t bt_output, buffer_t bt_output_arm) {
    return false; // This abstract implementation should never be called
}

template<class T> bool doCopy(Tester<T> *tester, const char *cacheDir, buffer_t bt_input, buffer_t bt_output, buffer_t bt_output_arm) {
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
                    if (abs(actual_value - expected_value) > EPSILON) {
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

    buffer_t make_planar_image(int width, int height, int channels, T host[]) {
        buffer_t bt_input = { 0 };
        bt_input.host = (uint8_t*)&host[0];
        bt_input.stride[0] = 1;
        bt_input.extent[0] = width;
        bt_input.stride[1] = width;
        bt_input.extent[1] = height;
        bt_input.stride[2] = width * height;
        bt_input.extent[2] = channels;
        bt_input.elem_size = sizeof(T);
        return bt_input;
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
            for (int i = 0; i < std::min(bt.extent[0], 10); i++) {
                std::cout << " [";
                for (int k = 0; k < bt.extent[2]; k++) {
                    std::cout << std::fixed << std::setprecision(1);
                    if (k > 0) {
                        std::cout << std::setw(4);
                    }
                    std::cout << +((T*)bt.host)[i * bt.stride[0] +
                                                j * bt.stride[1] +
                                                k * bt.stride[2]];
                }
                std::cout << "]";
            }

            std::cout << std::endl;
        }
    }

    int sdk_version() {
        char sdk_ver_str[32] = "0";
        __system_property_get("ro.build.version.sdk", sdk_ver_str);
        int sdk_ver = atoi(sdk_ver_str);
        LOGI("sdk ver=%d\n", sdk_ver);
        return sdk_ver;
    }

  public:
    bool test(const char* cacheDir,
              buffer_t bt_input, buffer_t bt_output, buffer_t bt_output_arm,
              filter_t generated_rs, filter_t generated_arm) {
        const int n_reps = 500;
        if (debug_level > 0) {
            std::cout << "Input:\n";
            print(bt_input);
        }

        timing rs_with_copying(generated_rs, cacheDir, &bt_input, &bt_output);
        int error;
        if (error = rs_with_copying.run(n_reps, true) != 0) {
            LOGI("Halide returned error: %d", error);
        }
       timing rs(generated_rs, cacheDir, &bt_input, &bt_output);
        if (error = rs.run(n_reps, false) != 0) {
            LOGI("Halide returned error: %d", error);
        }
        if (bt_output.dev) {
            timeval t1, t2;
            gettimeofday(&t1, NULL);
            halide_copy_to_host(NULL, &bt_output);
            gettimeofday(&t2, NULL);
            if (error) {
                return(error);
            }
            double t = (t2.tv_sec - t1.tv_sec) * 1000.0 + (t2.tv_usec - t1.tv_usec) / 1000.0;
            LOGI("Copied to host in %fms\n", t);
        }

        if (debug_level > 0) {
            std::cout << "RS Output:\n";
            print(bt_output);
        }

        timing arm(generated_arm, cacheDir, &bt_input, &bt_output_arm);
        if (error = arm.run(n_reps, false) != 0) {
            LOGI("Halide returned error: %d\n", error);
        }

       if (debug_level > 0) {
            std::cout << "Halide ARM Output:\n";
            print(bt_output_arm);
        }

        LOGI("Out of %d runs best times are:\n"
            "RS:            %fms(@%d)\n"
            "RS(with copy): %fms(@%d)\n"
            "ARM:           %fms(@%d)\n",
            n_reps,
            rs.best_t, rs.best_rep,
            rs_with_copying.best_t, rs_with_copying.best_rep,
            arm.best_t, arm.best_rep);
        LOGI("Out of %d runs worst times are:\n"
            "RS:            %fms(@%d)\n"
            "RS(with copy): %fms(@%d)\n"
            "ARM:           %fms(@%d)\n",
            n_reps,
            rs.worst_t, rs.worst_rep,
            rs_with_copying.worst_t, rs_with_copying.worst_rep,
            arm.worst_t, arm.worst_rep);

        return validate(bt_output, bt_output_arm);
    }
  public:
    void runTest(const char* cacheDir) {
        // Throttle sample image size to make sure we don't crash with out of memory.
        const int width = 800 / sizeof(T);
        const int height = 768 / sizeof(T);
        const int channels = 4;

        T input_image[width * height * channels];
        T output_image[width * height * channels];
        T output_image_arm[width * height * channels];
        bool correct = true;

        if (sdk_version() >= 23) { // 3-dim image support was not added until 23
            buffer_t bt_input = make_planar_image(width, height, channels, input_image);
            for (int i = 0; i < std::min(bt_input.extent[0], width); i++) {
                for (int j = 0; j < std::min(bt_input.extent[1], height); j++) {
                    for (int k = 0; k < bt_input.extent[2]; k++) {
                        input_image[i * bt_input.stride[0] +
                                    j * bt_input.stride[1] +
                                    k * bt_input.stride[2]] = ((i + j) % 2) * 6;
                    }
                }
            }
            bt_input.host_dirty = true;
            buffer_t bt_output =
                make_planar_image(width, height, channels, output_image);
            buffer_t bt_output_arm =
                make_planar_image(width, height, channels, output_image_arm);

            LOGI("\n*** Planar blur:\n");
            if (!doBlur<T>(this, cacheDir, bt_input, bt_output, bt_output_arm)) {
                correct = false;
            }
            LOGI("\n*** Planar copy:\n");
            if (!doCopy<T>(this, cacheDir, bt_input, bt_output, bt_output_arm)) {
                correct = false;
            }
        }

        buffer_t bt_interleaved_input =
            make_interleaved_image(width, height, channels, input_image);
        for (int i = 0; i < std::min(bt_interleaved_input.extent[0], width); i++) {
            for (int j = 0; j < std::min(bt_interleaved_input.extent[1], height); j++) {
                for (int k = 0; k < bt_interleaved_input.extent[2]; k++) {
                    input_image[i * bt_interleaved_input.stride[0] +
                                j * bt_interleaved_input.stride[1] +
                                k * bt_interleaved_input.stride[2]] = ((i + j) % 2) * 6;
                }
            }
        }
        bt_interleaved_input.host_dirty = true;

        buffer_t bt_interleaved_output =
            make_interleaved_image(width, height, channels, output_image);
        buffer_t bt_interleaved_output_arm =
            make_interleaved_image(width, height, channels, output_image_arm);

        LOGI("\n*** Interleaved(vectorized) blur:\n");
        if (!doBlur(this,
                    cacheDir,
                    bt_interleaved_input, bt_interleaved_output,
                    bt_interleaved_output_arm)) {
            correct = false;
        }
        LOGI("\n*** Interleaved(vectorized) copy:\n");
        if (!doCopy(this,
                    cacheDir,
                    bt_interleaved_input,
                    bt_interleaved_output, bt_interleaved_output_arm)) {
            correct = false;
        }

        if (correct) {
            LOGI("Done!\n");
        } else {
            LOGI("Failed!\n");
        }
    }
};

template<> bool doBlur<float>(Tester<float> *tester, const char *cacheDir, buffer_t bt_input, buffer_t bt_output, buffer_t bt_output_arm) {
    return tester->test(cacheDir,
                        bt_input,
                        bt_output, bt_output_arm,
                        generated_blur_rs_float,
                        generated_blur_arm_float);
}

template<> bool doCopy<float>(Tester<float> *tester, const char *cacheDir, buffer_t bt_input, buffer_t bt_output, buffer_t bt_output_arm) {
    return tester->test(cacheDir,
                        bt_input,
                        bt_output, bt_output_arm,
                        generated_copy_rs_float,
                        generated_copy_arm_float);
}

template<> bool doBlur<uint8_t>(Tester<uint8_t> *tester, const char *cacheDir, buffer_t bt_input, buffer_t bt_output, buffer_t bt_output_arm) {
    return tester->test(cacheDir,
                        bt_input,
                        bt_output, bt_output_arm,
                        generated_blur_rs_uint8,
                        generated_blur_arm_uint8);
}

template<> bool doCopy<uint8_t>(Tester<uint8_t> *tester, const char *cacheDir, buffer_t bt_input, buffer_t bt_output, buffer_t bt_output_arm) {
    return tester->test(cacheDir,
                        bt_input,
                        bt_output, bt_output_arm,
                        generated_copy_rs_uint8,
                        generated_copy_arm_uint8);
}

int main(int argc, char **argv) {
    int debug_level = 0;
    if (argc > 1) {
        debug_level = atoi(argv[1]);
    }
    LOGI("\nvvvv vvvv vvvv");
    const char *cacheDir = "/data/tmp";
    LOGI("\nTesting uint8...\n");
    (new Tester<uint8_t>(debug_level))->runTest(cacheDir);
    LOGI("---- ---- ----");
    LOGI("\nTesting float...\n");
    (new Tester<float>(debug_level))->runTest(cacheDir);
    LOGI("^^^^ ^^^^ ^^^^\n");
}

extern "C" {
JNIEXPORT void JNICALL Java_com_example_hellohaliderenderscript_HalideRenderscriptActivity_runTest(JNIEnv *env, jobject obj, jstring jCacheDir) {
    const char *pchCacheDir = env->GetStringUTFChars(jCacheDir, 0);
    (new Tester<uint8_t>())->runTest(pchCacheDir);
    (new Tester<float>())->runTest(pchCacheDir);
    env->ReleaseStringUTFChars(jCacheDir, pchCacheDir);
}
}
