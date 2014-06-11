#include <jni.h>
#include <android/log.h>
#include <android/bitmap.h>
#include <android/native_window_jni.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

#include "halide.h"

#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,"halide_native",__VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,"halide_native",__VA_ARGS__)

#define DEBUG 1

extern "C" void halide_set_error_handler(int (*handler)(void *user_context, const char *));
extern "C" int halide_host_cpu_count();
extern "C" int64_t halide_current_time_ns();
extern "C" int halide_copy_to_host(void *, buffer_t *);
extern "C" int halide_copy_to_dev(void *, buffer_t *);
extern "C" int halide_dev_malloc(void *, buffer_t *);
extern "C" int halide_dev_free(void *, buffer_t *);

extern "C" int halide_printf(void *user_context, const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args,fmt);
    int result = __android_log_vprint(ANDROID_LOG_DEBUG, "halide", fmt, args);
    __builtin_va_end(args);
    return result;
//    LOGD("%s", ...);
}


#include <EGL/egl.h>
#include <GLES2/gl2.h>

#define USED_GL_FUNCTIONS                                               \

extern "C" void *halide_opengl_get_proc_address(void *user_context, const char *name) {
    static struct {
        const char *name;
        void *func;
    } tab[] = {
        { "glActiveTexture", (void*)&glActiveTexture },
        { "glAttachShader", (void*)&glAttachShader },
        { "glBindBuffer", (void*)&glBindBuffer },
        { "glBindFramebuffer", (void*)&glBindFramebuffer },
        { "glBindTexture", (void*)&glBindTexture },
        { "glBufferData", (void*)&glBufferData },
        { "glCheckFramebufferStatus", (void*)&glCheckFramebufferStatus },
        { "glCompileShader", (void*)&glCompileShader },
        { "glCreateProgram", (void*)&glCreateProgram },
        { "glCreateShader", (void*)&glCreateShader },
        { "glDeleteBuffers", (void*)&glDeleteBuffers },
        { "glDeleteFramebuffers", (void*)&glDeleteFramebuffers },
        { "glDeleteProgram", (void*)&glDeleteProgram },
        { "glDeleteShader", (void*)&glDeleteShader },
        { "glDeleteTextures", (void*)&glDeleteTextures },
        { "glDisable", (void*)&glDisable },
        { "glDisableVertexAttribArray", (void*)&glDisableVertexAttribArray },
//        { "glDrawBuffers", (void*)&glDrawBuffers },
        { "glDrawElements", (void*)&glDrawElements },
        { "glEnableVertexAttribArray", (void*)&glEnableVertexAttribArray },
        { "glFramebufferTexture2D", (void*)&glFramebufferTexture2D },
        { "glGenBuffers", (void*)&glGenBuffers },
        { "glGenFramebuffers", (void*)&glGenFramebuffers },
        { "glGenTextures", (void*)&glGenTextures },
        { "glGetAttribLocation", (void*)&glGetAttribLocation },
        { "glGetError", (void*)&glGetError },
        { "glGetProgramInfoLog", (void*)&glGetProgramInfoLog },
        { "glGetProgramiv", (void*)&glGetProgramiv },
        { "glGetShaderInfoLog", (void*)&glGetShaderInfoLog },
        { "glGetShaderiv", (void*)&glGetShaderiv },
//        { "glGetTexImage", (void*)&glGetTexImage },
//        { "glGetTexLevelParameteriv", (void*)&glGetTexLevelParameteriv },
        { "glGetUniformLocation", (void*)&glGetUniformLocation },
        { "glLinkProgram", (void*)&glLinkProgram },
        { "glReadPixels", (void*)&glReadPixels },
        { "glPixelStorei", (void*)&glPixelStorei },
        { "glShaderSource", (void*)&glShaderSource },
        { "glTexImage2D", (void*)&glTexImage2D },
        { "glTexParameteri", (void*)&glTexParameteri },
        { "glTexSubImage2D", (void*)&glTexSubImage2D },
        { "glUniform1fv", (void*)&glUniform1fv },
        { "glUniform1iv", (void*)&glUniform1iv },
        { "glUniform2iv", (void*)&glUniform2iv },
        { "glUseProgram", (void*)&glUseProgram },
        { "glVertexAttribPointer", (void*)&glVertexAttribPointer },
        { "glViewport", (void*)&glViewport },
        { NULL, NULL },
    };
    for (unsigned i = 0; tab[i].name != NULL; i++) {
        if (strcmp(name, tab[i].name) == 0) {
            return tab[i].func;
        }
    }
    LOGD("get_proc failed: %s\n", name);
    return NULL;
}

extern "C" int halide_opengl_create_context(void *user_context) {
    if (eglGetCurrentContext() != EGL_NO_CONTEXT)
        return 0;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, 0, 0)) {
        LOGE("Could not initialize EGL display %d", eglGetError());
        return 1;
    }

    // eglBindAPI(EGL_OPENGL_ES_API);

    EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLConfig config;
    int numconfig;
    eglChooseConfig(display, attribs, &config, 1, &numconfig);
    if (numconfig != 1) {
        LOGE(
            "Error: eglChooseConfig(): config not found %d - %d.\n", eglGetError(), numconfig);
        exit(-1);
    }

    GLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                                          context_attribs);
    if (context == EGL_NO_CONTEXT) {
        LOGE(
            "Error: eglCreateContext failed - %d.\n", eglGetError());
        exit(-1);
    }

    GLint surface_attribs[] = {
        EGL_WIDTH, 320,
        EGL_HEIGHT, 200,
        EGL_NONE
    };
    EGLSurface surface = eglCreatePbufferSurface(display, config,  surface_attribs);
    if (surface == EGL_NO_SURFACE) {
        LOGE("Error: Could not create EGL window surface: %d\n", eglGetError());
        exit(-1);
    }

    eglMakeCurrent(display, surface, surface, context);
    LOGD("Created new OpenGL context\n");
    return 0;
}

static int handler(void */* user_context */, const char *msg) {
    LOGE("%s", msg);
}

static int LockBitmap(JNIEnv *env, buffer_t *buf, jobject bitmap) {
    AndroidBitmapInfo info;
    AndroidBitmap_getInfo(env, bitmap, &info);

    buf->extent[0] = info.width;
    buf->extent[1] = info.height;
    buf->extent[2] = 4;
    buf->extent[3] = 0;
    buf->stride[0] = 4;
    buf->stride[1] = info.stride;
    buf->stride[2] = 1;
    buf->stride[3] = 0;
    buf->min[0] = 0;
    buf->min[1] = 0;
    buf->min[2] = 0;
    buf->elem_size = 1;

    if (int err = AndroidBitmap_lockPixels(env, bitmap, reinterpret_cast<void**>(&buf->host))) {
        LOGE("AndroidBitmap_lockPixels failed with error code %d.\n", err);
        return 1;
    }
    return 0;
}

extern "C" {
JNIEXPORT void JNICALL Java_com_example_hellohalide_HelloHalide_processFrame(
    JNIEnv *env, jobject obj, jobject src, jobject dst) {

    static bool first_call = true;
    static unsigned counter = 0;
    static unsigned times[16];
    if (first_call) {
        LOGD("According to Halide, host system has %d cpus\n", halide_host_cpu_count());
        first_call = false;
        for (int t = 0; t < 16; t++) times[t] = 0;
    }

    halide_set_error_handler(handler);

    buffer_t srcBuf = {0};
    buffer_t dstBuf = {0};
    if (LockBitmap(env, &srcBuf, src)) return;
    if (LockBitmap(env, &dstBuf, dst)) {
        AndroidBitmap_unlockPixels(env, src);
        return;
    }

    int64_t t1 = halide_current_time_ns();


    srcBuf.host_dirty = 1;
    if (int err = halide(&srcBuf, &dstBuf)) {
        LOGD("Halide filter failed with error code %d\n", err);
    }
    halide_copy_to_host(NULL, &dstBuf);
    halide_dev_free(NULL, &dstBuf);
    halide_dev_free(NULL, &srcBuf);

    int64_t t2 = halide_current_time_ns();
    unsigned elapsed_us = (t2 - t1)/1000;

    times[counter & 15] = elapsed_us;
    counter++;
    unsigned min = times[0];
    for (int i = 1; i < 16; i++) {
        if (times[i] < min) min = times[i];
    }
    LOGD("Time taken: %d (%d)", elapsed_us, min);

    AndroidBitmap_unlockPixels(env, src);
    AndroidBitmap_unlockPixels(env, dst);
}
}
