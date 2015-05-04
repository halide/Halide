#include <jni.h>
#include <android/log.h>

#include "halide_generated_8bit.h"
#include "halide_generated_16bit.h"
#include "halide_generated_32bit.h"
#include "HalideRuntime.h"

#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,"halide_native",__VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,"halide_native",__VA_ARGS__)

extern "C" {
JNIEXPORT jboolean JNICALL Java_com_halide_arm_TestActivity_runTest8bit(
    JNIEnv *env, jobject obj) {

    static buffer_t srcBuf = {0};
    static buffer_t dstBuf = {0};

    const int w = 128;
    uint8_t input_image[w];
    for (int i = 0; i < w; i++) {
        input_image[i] = (i + (1<<8) - 10) % (1<<8);
    }
    uint8_t output_image[w];

    srcBuf.host = (uint8_t*)input_image;
    srcBuf.extent[0] = w;
    srcBuf.stride[0] = 1;
    srcBuf.elem_size = 1;

    dstBuf.host = (uint8_t*)output_image;
    dstBuf.extent[0] = w;
    dstBuf.stride[0] = 1;
    dstBuf.elem_size = 1;

    halide_generated_8bit(&srcBuf, &dstBuf);

    bool match = true;
    for (uint8_t i = 0; i < w; i++) {
        uint8_t expected = (input_image[i] + input_image[i]) % (1<<8) / 2;
        if (output_image[i] != expected) {
            LOGE("@%d (%d+%d)/2 expected to be %d, but turned out to be %d\n",
                i, input_image[i], input_image[i], expected, output_image[i]);
            match = false;
        }
    }
    return match? JNI_TRUE: JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_halide_arm_TestActivity_runTest16bit(
    JNIEnv *env, jobject obj) {

    // Make these static so that we can reuse device allocations across frames.
    static buffer_t srcBuf = {0};
    static buffer_t dstBuf = {0};

    const int w = 128;
    uint16_t input_image[w];
    for (uint16_t i = 0; i < w; i++) {
        input_image[i] = (i + (1<<16) - 10) % (1<<16);
    }
    uint16_t output_image[w];

    srcBuf.host = (uint8_t*)input_image;
    srcBuf.extent[0] = w;
    srcBuf.stride[0] = 1;
    srcBuf.elem_size = 2;

    dstBuf.host = (uint8_t*)output_image;
    dstBuf.extent[0] = w;
    dstBuf.stride[0] = 1;
    dstBuf.elem_size = 2;

    halide_generated_16bit(&srcBuf, &dstBuf);

    bool match = true;
    for (int i = 0; i < w; i++) {
        uint16_t expected = (input_image[i] + input_image[i]) % (1<<16) / 2;
        if (output_image[i] != expected) {
            LOGE("@%d expected %d didn't match actual %d\n", i, expected, output_image[i]);
            match = false;
        }
    }
    return match? JNI_TRUE: JNI_FALSE;
}


JNIEXPORT jboolean JNICALL Java_com_halide_arm_TestActivity_runTest32bit(
    JNIEnv *env, jobject obj) {

    // Make these static so that we can reuse device allocations across frames.
    static buffer_t srcBuf = {0};
    static buffer_t dstBuf = {0};

    const int w = 128;
    uint32_t input_image[w];
    for (uint64_t i = 0; i < w; i++) {
        input_image[i] = (i + (uint64_t(1)<<32)-10) % (uint64_t(1)<<32);
    }
    uint32_t output_image[w];

    srcBuf.host = (uint8_t*)input_image;
    srcBuf.extent[0] = w;
    srcBuf.stride[0] = 1;
    srcBuf.elem_size = 4;

    dstBuf.host = (uint8_t*)output_image;
    dstBuf.extent[0] = w;
    dstBuf.stride[0] = 1;
    dstBuf.elem_size = 4;

    halide_generated_32bit(&srcBuf, &dstBuf);

    bool match = true;
    for (int i = 0; i < w; i++) {
        uint32_t expected = (input_image[i] + input_image[i]) % (uint64_t(1)<<32) / 2;
        if (output_image[i] != expected) {
            LOGE("@%d expected %u didn't match actual %u\n", i, expected, output_image[i]);
            match = false;
        }
    }
    return match? JNI_TRUE: JNI_FALSE;
}
}
