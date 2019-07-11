#include "HalideRuntime.h"
#include "printer.h"

extern "C" {

#define EGLAPI
#define EGLAPIENTRY

typedef int32_t EGLint;
typedef unsigned int EGLBoolean;
typedef void *EGLContext;
typedef void *EGLDisplay;
typedef void *EGLNativeDisplayType;
typedef void *EGLConfig;
typedef void *EGLSurface;
#define EGL_NO_CONTEXT                  ((EGLContext)0)
#define EGL_DEFAULT_DISPLAY             ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY                  ((EGLDisplay)0)
#define EGL_NO_SURFACE                  ((EGLSurface)0)

#define EGL_ALPHA_SIZE                  0x3021
#define EGL_BLUE_SIZE                   0x3022
#define EGL_GREEN_SIZE                  0x3023
#define EGL_RED_SIZE                    0x3024
#define EGL_SURFACE_TYPE                0x3033
#define EGL_NONE                        0x3038
#define EGL_RENDERABLE_TYPE             0x3040
#define EGL_HEIGHT                      0x3056
#define EGL_WIDTH                       0x3057
#define EGL_CONTEXT_CLIENT_VERSION      0x3098

#define EGL_PBUFFER_BIT                 0x0001
#define EGL_OPENGL_ES2_BIT              0x0004

EGLAPI EGLint EGLAPIENTRY eglGetError(void);
EGLAPI EGLContext EGLAPIENTRY eglGetCurrentContext(void);
EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType display_id);
EGLAPI EGLBoolean EGLAPIENTRY eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor);
EGLAPI EGLBoolean EGLAPIENTRY eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *configs, EGLint config_size,
                           EGLint *num_config);
EGLAPI EGLContext EGLAPIENTRY eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context,
                            const EGLint *attrib_list);
EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                   const EGLint *attrib_list);
EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                          EGLSurface read, EGLContext ctx);

EGLAPI void *eglGetProcAddress(const char *procname);


extern int strcmp(const char *, const char *);

WEAK int halide_opengl_create_context(void *user_context) {
    if (eglGetCurrentContext() != EGL_NO_CONTEXT)
        return 0;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, 0, 0)) {
        error(user_context) << "Could not initialize EGL display: " << eglGetError();
        return 1;
    }

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
        error(user_context) << "eglChooseConfig(): config not found: "
                            << eglGetError() << " - " << numconfig;
        return -1;
    }

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                                          context_attribs);
    if (context == EGL_NO_CONTEXT) {
        error(user_context) << "Error: eglCreateContext failed: " << eglGetError();
        return -1;
    }

    EGLint surface_attribs[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE
    };
    EGLSurface surface = eglCreatePbufferSurface(display, config,  surface_attribs);
    if (surface == EGL_NO_SURFACE) {
        error(user_context) << "Error: Could not create EGL window surface: " << eglGetError();
        return -1;
    }

    eglMakeCurrent(display, surface, surface, context);
    return 0;
}

WEAK void *halide_opengl_get_proc_address(void *user_context, const char *name) {
    return (void*)eglGetProcAddress(name);
}

}  // extern "C"
