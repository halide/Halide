#include "HalideRuntime.h"
#include "printer.h"
#include "scoped_mutex_lock.h"

#define USE_AGL 0
#if USE_AGL
extern "C" void *aglChoosePixelFormat(void *, int, const int *);
extern "C" void *aglCreateContext(void *, void *);
extern "C" int aglGetError();
extern "C" void aglDestroyPixelFormat(void *);
extern "C" unsigned char aglSetCurrentContext(void *);
#endif

#if !USE_AGL
namespace Halide {
namespace Runtime {
namespace Internal {
namespace OpenGL {

WEAK halide_mutex cgl_functions_mutex;
WEAK bool cgl_initialized = false;
WEAK int (*CGLChoosePixelFormat)(int *attributes, void **pixel_format_result, int *num_formats);
WEAK int (*CGLCreateContext)(void *pixel_format, void *share_context, void **context_Result);
WEAK int (*CGLDestroyPixelFormat)(void *);
WEAK int (*CGLSetCurrentContext)(void *);

}  // namespace OpenGL
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::Internal::OpenGL;
#endif

extern "C" {

WEAK void *halide_opengl_get_proc_address(void *user_context, const char *name) {
    static void *dylib = nullptr;
    if (!dylib) {
        dylib = halide_load_library(
            "/System/Library/Frameworks/OpenGL.framework/Versions/Current/OpenGL");
        if (!dylib) {
            return nullptr;
        }
    }
    return halide_get_library_symbol(dylib, name);
}

// Initialize OpenGL
WEAK int halide_opengl_create_context(void *user_context) {
#if USE_AGL
    void *ctx = nullptr;

    int attrib[] = {4 /* AGL_RGBA */, 0 /* Sentinel */};
    void *pf = aglChoosePixelFormat(nullptr, 0, attrib);
    if (!pf) {
        error(user_context) << "Could not create pixel format.";
        return halide_error_code_generic_error;
    }
    ctx = aglCreateContext(pf, nullptr);
    if (!ctx || aglGetError()) {
        error(user_context) << "Could not create context.";
        return halide_error_code_generic_error;
    }
    aglDestroyPixelFormat(pf);
    if (!aglSetCurrentContext(ctx)) {
        error(user_context) << "Could not activate OpenGL context.";
        return halide_error_code_generic_error;
    }
#else
    {  // locking scope
        ScopedMutexLock lock(&cgl_functions_mutex);

        if (!cgl_initialized) {
            if ((CGLChoosePixelFormat =
                     (int (*)(int *, void **, int *))halide_opengl_get_proc_address(user_context, "CGLChoosePixelFormat")) == nullptr) {
                return halide_error_code_generic_error;
            }
            if ((CGLCreateContext =
                     (int (*)(void *, void *, void **))halide_opengl_get_proc_address(user_context, "CGLCreateContext")) == nullptr) {
                return halide_error_code_generic_error;
            }
            if ((CGLDestroyPixelFormat =
                     (int (*)(void *))halide_opengl_get_proc_address(user_context, "CGLDestroyPixelFormat")) == nullptr) {
                return halide_error_code_generic_error;
            }
            if ((CGLSetCurrentContext =
                     (int (*)(void *))halide_opengl_get_proc_address(user_context, "CGLSetCurrentContext")) == nullptr) {
                return halide_error_code_generic_error;
            }
        }
        cgl_initialized = true;
    }

    void *ctx = nullptr;
    int attribs[] = {
        /* 5 kCGLPFADoubleBuffer */
        72,      // kCGLPFANoRecovery
        96,      // kCGLPFAAllowOfflineRenderers
        99,      // kCGLPFAOpenGLProfile
        0x1000,  // kCGLOGLPVersion_Legacy -- 0x3200 is kCGLOGLPVersion_3_2_Core -- kCGLOGLPVersion_GL4_Core is 0x4100
        0        // sentinel ending list
    };

    void *fmt;
    int numFormats = 0;
    if (CGLChoosePixelFormat(attribs, &fmt, &numFormats) != 0) {
        return halide_error_code_generic_error;
    }
    if (CGLCreateContext(fmt, nullptr, &ctx) != 0) {
        CGLDestroyPixelFormat(fmt);
        return halide_error_code_generic_error;
    }
    CGLSetCurrentContext(ctx);
#endif
    return halide_error_code_success;
}
}
