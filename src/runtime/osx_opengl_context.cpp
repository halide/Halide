#include "runtime_internal.h"
#include "HalideRuntime.h"

extern "C" void *dlopen(const char *, int);
extern "C" void *dlsym(void *, const char *);
extern "C" void *aglChoosePixelFormat(void *, int, const int *);
extern "C" void *aglCreateContext(void *, void *);
extern "C" int aglGetError();
extern "C" void aglDestroyPixelFormat(void *);
extern "C" unsigned char aglSetCurrentContext(void *);

extern "C" {

WEAK void *halide_opengl_get_proc_address(void *user_context, const char *name) {
    static void *dylib = NULL;
    if (!dylib) {
        dylib = dlopen(
            "/System/Library/Frameworks/OpenGL.framework/Versions/Current/OpenGL",
            1);
        if (!dylib) return NULL;
    }
    return dlsym(dylib, name);
}

// Initialize OpenGL
WEAK int halide_opengl_create_context(void *user_context) {
    void *ctx = NULL;

    int attrib[] = {4 /* AGL_RGBA */, 0 /* Sentinel */};
    void *pf = aglChoosePixelFormat(NULL, 0, attrib);
    if (!pf) {
        halide_error(user_context, "Could not create pixel format\n");
        return -1;
    }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx || aglGetError()) {
        halide_error(user_context, "Could not create context\n");
        return -1;
    }
    aglDestroyPixelFormat(pf);
    if (!aglSetCurrentContext(ctx)) {
        halide_error(user_context, "Could not activate OpenGL context\n");
        return -1;
    }
    return 0;
}

}
