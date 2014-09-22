#include <jni.h>
#include <android/log.h>
#include <android/bitmap.h>
#include <android/native_window_jni.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

#include "halide_gl_filter.h"

#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,"halide_native",__VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,"halide_native",__VA_ARGS__)

#define DEBUG 1

extern "C"
JNIEXPORT void JNICALL Java_org_halide_1lang_hellohalidegl_HalideGLView_processTextureHalide(
    JNIEnv *env, jobject obj, jint dst, jint width, jint height) {

    buffer_t dstBuf = {0};
    dstBuf.extent[0] = width;
    dstBuf.extent[1] = height;
    dstBuf.extent[2] = 4;
    dstBuf.stride[0] = 4;
    dstBuf.stride[1] = 4 * width;
    dstBuf.stride[2] = 1;
    dstBuf.min[0] = 0;
    dstBuf.min[1] = 0;
    dstBuf.min[2] = 0;
    dstBuf.elem_size = 1;
    dstBuf.host = NULL;
    dstBuf.dev = dst;

    static float time = 0.0f;
    if (int err = halide_gl_filter(time, &dstBuf)) {
        LOGD("Halide filter failed with error code %d\n", err);
    }
    time += 1.0f/16.0f;
}

extern "C" void halide_opengl_context_lost(void *user_context);

extern "C"
JNIEXPORT void JNICALL Java_org_halide_1lang_hellohalidegl_HalideGLView_halideContextLost(
    JNIEnv * env, jobject obj) {

    halide_opengl_context_lost(NULL);
}
