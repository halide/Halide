#include <jni.h>
#include <android/log.h>
#include <android/bitmap.h>
#include <android/native_window_jni.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

#include "hello.h"
#include "HalideRuntime.h"
#include "HalideRuntimeOpenCL.h"

#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,"halide_native",__VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,"halide_native",__VA_ARGS__)

#define DEBUG 1

extern "C" int halide_host_cpu_count();
extern "C" int halide_start_clock(void *user_context);
extern "C" int64_t halide_current_time_ns();

void handler(void */* user_context */, const char *msg) {
    LOGE("%s", msg);
}

extern "C" {
JNIEXPORT void JNICALL Java_com_example_hellohalide_CameraPreview_processFrame(
    JNIEnv *env, jobject obj, jbyteArray jSrc, jint j_w, jint j_h, jint j_orientation, jobject surf) {

    const int w = j_w, h = j_h, orientation = j_orientation;

    halide_start_clock(NULL);
    halide_set_error_handler(handler);

    unsigned char *src = (unsigned char *)env->GetByteArrayElements(jSrc, NULL);
    if (!src) {
        LOGD("src is null\n");
        return;
    }

    LOGD("[output window size] j_w = %d, j_h = %d", j_w, j_h);
    LOGD("[src array length] jSrc.length = %d", env->GetArrayLength(jSrc));

    ANativeWindow *win = ANativeWindow_fromSurface(env, surf);


    static bool first_call = true;
    static unsigned counter = 0;
    static unsigned times[16];
    if (first_call) {
        LOGD("According to Halide, host system has %d cpus\n", halide_host_cpu_count());
        LOGD("Resetting buffer format");
        ANativeWindow_setBuffersGeometry(win, w, h, 0);
        first_call = false;
        for (int t = 0; t < 16; t++) times[t] = 0;
    }

    ANativeWindow_Buffer buf;
    ARect rect = {0, 0, w, h};

    if (int err = ANativeWindow_lock(win, &buf, NULL)) {
        LOGD("ANativeWindow_lock failed with error code %d\n", err);
        return;
    }

    uint8_t *dst = (uint8_t *)buf.bits;

    // If we're using opencl, use the gpu backend for it.
#if COMPILING_FOR_OPENCL
    halide_opencl_set_device_type("gpu");
#endif

    // Make these static so that we can reuse device allocations across frames.
    static halide_buffer_t srcBuf = {0};
    static halide_dimension_t srcDim[2];
    static halide_buffer_t dstBuf = {0};
    static halide_dimension_t dstDim[2];

    if (dst) {
        srcBuf.host = (uint8_t *)src;
        srcBuf.set_host_dirty();
        srcBuf.dim = srcDim;
        srcBuf.dim[0].min = 0;
        srcBuf.dim[0].extent = w;
        srcBuf.dim[0].stride = 1;
        srcBuf.dim[1].min = 0;
        srcBuf.dim[1].extent = h;
        srcBuf.dim[1].stride = w;
        srcBuf.type = halide_type_of<uint8_t>();

        if (orientation >= 180) {
            // Camera sensor is probably upside down (e.g. Nexus 5x)
            srcBuf.host += w*h-1;
            srcBuf.dim[0].stride = -1;
            srcBuf.dim[1].stride = -w;
        }

        dstBuf.host = dst;
        dstBuf.dim = dstDim;
        dstBuf.dim[0].min = 0;
        dstBuf.dim[0].extent = w;
        dstBuf.dim[0].stride = 1;
        dstBuf.dim[1].min = 0;
        dstBuf.dim[1].extent = h;
        dstBuf.dim[1].stride = w;
        dstBuf.type = halide_type_of<uint8_t>();

        // Just set chroma to gray.
        memset(dst + w*h, 128, (w*h)/2);

        int64_t t1 = halide_current_time_ns();
        hello(&srcBuf, &dstBuf);

        halide_copy_to_host(NULL, &dstBuf);

        int64_t t2 = halide_current_time_ns();
        unsigned elapsed_us = (t2 - t1)/1000;


        times[counter & 15] = elapsed_us;
        counter++;
        unsigned min = times[0];
        for (int i = 1; i < 16; i++) {
            if (times[i] < min) min = times[i];
        }
        LOGD("Time taken: %d (%d)", elapsed_us, min);
    }

    ANativeWindow_unlockAndPost(win);
    ANativeWindow_release(win);

    env->ReleaseByteArrayElements(jSrc, (jbyte *)src, 0);
}
}
