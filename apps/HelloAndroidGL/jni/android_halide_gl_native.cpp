#include <android/bitmap.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "HalideBuffer.h"
#include "HalideRuntimeOpenGL.h"
#include "halide_gl_filter.h"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "halide_native", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "halide_native", __VA_ARGS__)

void *const user_context = NULL;

extern "C" JNIEXPORT void JNICALL Java_org_halide_1lang_hellohalidegl_HalideGLView_processTextureHalide(
    JNIEnv *env, jobject obj, jint dst, jint width, jint height) {

    auto dstBuf = Halide::Runtime::Buffer<uint8_t>::make_interleaved(NULL, width, height, 4);
    // If dst == 0, let Halide render directly to the current render target.
    if (dst == 0) {
        int result = halide_opengl_wrap_render_target(user_context, dstBuf);
        if (result != 0) {
            halide_error(user_context, "halide_opengl_wrap_render_target failed");
        }
    } else {
        int result = halide_opengl_wrap_texture(user_context, dstBuf, dst);
        if (result != 0) {
            halide_error(user_context, "halide_opengl_wrap_texture failed");
        }
    }

    static float time = 0.0f;
    if (int err = halide_gl_filter(time, dstBuf)) {
        LOGD("Halide filter failed with error code %d\n", err);
    }
    time += 1.0f / 16.0f;

    uintptr_t detached = halide_opengl_detach_texture(user_context, dstBuf);
    if (detached != dst) {
        halide_error(user_context, "halide_opengl_detach_texture failed");
    }
}

extern "C" JNIEXPORT void JNICALL Java_org_halide_1lang_hellohalidegl_HalideGLView_halideContextLost(
    JNIEnv *env, jobject obj) {

    halide_opengl_context_lost(NULL);
}
