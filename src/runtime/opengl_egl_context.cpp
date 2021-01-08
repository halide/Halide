#include "HalideRuntime.h"
#include "printer.h"

extern "C" {

#define EGLAPI
#define EGLAPIENTRY
#define EGLAPIENTRYP EGLAPIENTRY *

typedef int32_t EGLint;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
typedef void *EGLContext;
typedef void *EGLDisplay;
typedef void *EGLNativeDisplayType;
typedef void *EGLConfig;
typedef void *EGLSurface;
typedef void *EGLDeviceEXT;

typedef EGLBoolean(EGLAPIENTRYP PFNEGLQUERYDEVICESEXTPROC)(
    EGLint max_devices, EGLDeviceEXT *devices, EGLint *num_devices);
typedef EGLDisplay(EGLAPIENTRYP PFNEGLGETPLATFORMDISPLAYEXTPROC)(
    EGLenum platform, void *native_display, const EGLint *attrib_list);

#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_SURFACE ((EGLSurface)0)

#define EGL_SUCCESS 0x3000

#define EGL_ALPHA_SIZE 0x3021
#define EGL_BLUE_SIZE 0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE 0x3024
#define EGL_SURFACE_TYPE 0x3033
#define EGL_NONE 0x3038
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_HEIGHT 0x3056
#define EGL_WIDTH 0x3057
#define EGL_CONTEXT_CLIENT_VERSION 0x3098

#define EGL_PLATFORM_DEVICE_EXT 0x313F

#define EGL_PBUFFER_BIT 0x0001
#define EGL_OPENGL_ES2_BIT 0x0004

#define EGL_FALSE 0
#define EGL_TRUE 1

EGLAPI EGLint EGLAPIENTRY eglGetError(void);
EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigs(EGLDisplay display,
                                            EGLConfig *configs,
                                            EGLint config_size,
                                            EGLint *num_config);
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
EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value);

extern int strcmp(const char *, const char *);

WEAK int halide_opengl_create_context(void *user_context) {
    if (eglGetCurrentContext() != EGL_NO_CONTEXT) {
        return 0;
    }

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) {
        PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT =
            reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(
                eglGetProcAddress("eglQueryDevicesEXT"));
        if (eglQueryDevicesEXT == nullptr) {
            return 1;
        }

        PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
            reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
                eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (eglGetPlatformDisplayEXT == nullptr) {
            return 1;
        }

        const int kMaxDevices = 32;
        EGLDeviceEXT egl_devices[kMaxDevices];
        EGLint num_devices = 0;
        EGLint egl_error = eglGetError();
        if (!eglQueryDevicesEXT(kMaxDevices, egl_devices, &num_devices) ||
            egl_error != EGL_SUCCESS) {
            return 1;
        }

        EGLBoolean initialized = EGL_FALSE;
        for (EGLint i = 0; i < num_devices; ++i) {
            display = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT,
                                               egl_devices[i], nullptr);
            if (eglGetError() == EGL_SUCCESS && display != EGL_NO_DISPLAY) {
                int major, minor;
                initialized = eglInitialize(display, &major, &minor);
                if (eglGetError() == EGL_SUCCESS && initialized == EGL_TRUE) {
                    break;
                }
            }
        }

        if (eglGetError() != EGL_SUCCESS || initialized != EGL_TRUE) {
            error(user_context) << "Could not initialize EGL display: "
                                << eglGetError();
            return 1;
        }
    }

    // clang-format off
    EGLint attribs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_NONE,            EGL_NONE
    };
    // clang-format on
    EGLConfig config;
    int numconfig;
    EGLBoolean result = eglChooseConfig(display, attribs, &config, 1, &numconfig);
    if (result != EGL_TRUE || numconfig != 1) {
        EGLint num_actual_configs;
        EGLBoolean result2 = eglGetConfigs(display, nullptr, 0, &num_actual_configs);
        if (result2 == EGL_FALSE) abort();
        EGLConfig *actual_configs = (EGLConfig *)__builtin_alloca(num_actual_configs * sizeof(EGLConfig));
        result2 = eglGetConfigs(display, actual_configs, num_actual_configs, &num_actual_configs);
        if (result2 == EGL_FALSE) abort();
        print(user_context) << "num_actual_configs=" << num_actual_configs << "\n";
        for (int i = 0; i < num_actual_configs; i++) {
            EGLint value;
#define EGL_ALPHA_SIZE 0x3021
#define EGL_BAD_ACCESS 0x3002
#define EGL_BAD_ALLOC 0x3003
#define EGL_BAD_ATTRIBUTE 0x3004
#define EGL_BAD_CONFIG 0x3005
#define EGL_BAD_CONTEXT 0x3006
#define EGL_BAD_CURRENT_SURFACE 0x3007
#define EGL_BAD_DISPLAY 0x3008
#define EGL_BAD_MATCH 0x3009
#define EGL_BAD_NATIVE_PIXMAP 0x300A
#define EGL_BAD_NATIVE_WINDOW 0x300B
#define EGL_BAD_PARAMETER 0x300C
#define EGL_BAD_SURFACE 0x300D
#define EGL_BLUE_SIZE 0x3022
#define EGL_BUFFER_SIZE 0x3020
#define EGL_CONFIG_CAVEAT 0x3027
#define EGL_CONFIG_ID 0x3028
#define EGL_CORE_NATIVE_ENGINE 0x305B
#define EGL_DEPTH_SIZE 0x3025
#define EGL_DRAW 0x3059
#define EGL_EXTENSIONS 0x3055
#define EGL_FALSE 0
#define EGL_GREEN_SIZE 0x3023
#define EGL_HEIGHT 0x3056
#define EGL_LARGEST_PBUFFER 0x3058
#define EGL_LEVEL 0x3029
#define EGL_MAX_PBUFFER_HEIGHT 0x302A
#define EGL_MAX_PBUFFER_PIXELS 0x302B
#define EGL_MAX_PBUFFER_WIDTH 0x302C
#define EGL_NATIVE_RENDERABLE 0x302D
#define EGL_NATIVE_VISUAL_ID 0x302E
#define EGL_NATIVE_VISUAL_TYPE 0x302F
#define EGL_NONE 0x3038
#define EGL_NON_CONFORMANT_CONFIG 0x3051
#define EGL_NOT_INITIALIZED 0x3001
#define EGL_PBUFFER_BIT 0x0001
#define EGL_PIXMAP_BIT 0x0002
#define EGL_READ 0x305A
#define EGL_RED_SIZE 0x3024
#define EGL_SAMPLES 0x3031
#define EGL_SAMPLE_BUFFERS 0x3032
#define EGL_SLOW_CONFIG 0x3050
#define EGL_STENCIL_SIZE 0x3026
#define EGL_SUCCESS 0x3000
#define EGL_SURFACE_TYPE 0x3033
#define EGL_TRANSPARENT_BLUE_VALUE 0x3035
#define EGL_TRANSPARENT_GREEN_VALUE 0x3036
#define EGL_TRANSPARENT_RED_VALUE 0x3037
#define EGL_TRANSPARENT_RGB 0x3052
#define EGL_TRANSPARENT_TYPE 0x3034
#define EGL_TRUE 1
#define EGL_VENDOR 0x3053
#define EGL_VERSION 0x3054
#define EGL_WIDTH 0x3057
#define EGL_WINDOW_BIT 0x0004
            print(user_context) << "Config #" << i << "\n";
            eglGetConfigAttrib(display, actual_configs[i], EGL_BUFFER_SIZE, &value);
            print(user_context) << "Buffer Size " << value << "\n";
            eglGetConfigAttrib(display, actual_configs[i], EGL_RED_SIZE, &value);
            print(user_context) << "Red Size " << value << "\n";
            eglGetConfigAttrib(display, actual_configs[i], EGL_GREEN_SIZE, &value);
            print(user_context) << "Green Size " << value << "\n";
            eglGetConfigAttrib(display, actual_configs[i], EGL_BLUE_SIZE, &value);
            print(user_context) << "Blue Size " << value << "\n";
            eglGetConfigAttrib(display, actual_configs[i], EGL_ALPHA_SIZE, &value);
            print(user_context) << "Alpha Size " << value << "\n";
            eglGetConfigAttrib(display, actual_configs[i], EGL_CONFIG_CAVEAT, &value);
            switch (value) {
            case EGL_NONE:
                print(user_context) << "EGL_CONFIG_CAVEAT EGL_NONE\n";
                break;
            case EGL_SLOW_CONFIG:
                print(user_context) << "EGL_CONFIG_CAVEAT EGL_SLOW_CONFIG\n";
                break;
            }
            eglGetConfigAttrib(display, actual_configs[i], EGL_CONFIG_ID, &value);
            print(user_context) << "Config ID " << value << "\n";

            eglGetConfigAttrib(display, actual_configs[i], EGL_DEPTH_SIZE, &value);
            print(user_context) << "Depth size " << value << "\n";

            eglGetConfigAttrib(display, actual_configs[i], EGL_MAX_PBUFFER_WIDTH, &value);
            print(user_context) << "Max pbuffer width " << value << "\n";
            eglGetConfigAttrib(display, actual_configs[i], EGL_MAX_PBUFFER_HEIGHT, &value);
            print(user_context) << "Max pbuffer height " << value << "\n";
            eglGetConfigAttrib(display, actual_configs[i], EGL_MAX_PBUFFER_PIXELS, &value);
            print(user_context) << "Max pbuffer pixels " << value << "\n";
            eglGetConfigAttrib(display, actual_configs[i], EGL_NATIVE_RENDERABLE, &value);
            print(user_context) << "Native renderable " << (value ? "true" : "false") << "\n";
            eglGetConfigAttrib(display, actual_configs[i], EGL_NATIVE_VISUAL_ID, &value);
            print(user_context) << "Native visual ID " << value << "\n";
            eglGetConfigAttrib(display, actual_configs[i], EGL_NATIVE_VISUAL_TYPE, &value);
            print(user_context) << "Native visual type " << value << "\n";
            eglGetConfigAttrib(display, actual_configs[i], EGL_SAMPLE_BUFFERS, &value);
            print(user_context) << "Sample Buffers " << value << "\n";
            eglGetConfigAttrib(display, actual_configs[i], EGL_SAMPLES, &value);
            print(user_context) << "Samples " << value << "\n";
            eglGetConfigAttrib(display, actual_configs[i], EGL_SURFACE_TYPE, &value);
            print(user_context) << "Surface type " << value << "\n";
            eglGetConfigAttrib(display, actual_configs[i], EGL_TRANSPARENT_TYPE, &value);
            print(user_context) << "--------------------------------------------------------------------------\n";
        }

        error(user_context) << "eglChooseConfig(): config not found: "
                            << " result=" << (int)result
                            << " eglGetError=" << eglGetError()
                            << " numConfig=" << numconfig;
        return -1;
    }

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                                          context_attribs);
    if (context == EGL_NO_CONTEXT) {
        error(user_context) << "Error: eglCreateContext failed: " << eglGetError();
        return -1;
    }

    EGLint surface_attribs[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surface_attribs);
    if (surface == EGL_NO_SURFACE) {
        error(user_context) << "Error: Could not create EGL window surface: " << eglGetError();
        return -1;
    }

    result = eglMakeCurrent(display, surface, surface, context);
    if (result != EGL_TRUE) {
        error(user_context) << "eglMakeCurrent fails: "
                            << " result=" << (int)result
                            << " eglGetError=" << eglGetError();
        return -1;
    }
    return 0;
}

WEAK void *halide_opengl_get_proc_address(void *user_context, const char *name) {
    return (void *)eglGetProcAddress(name);
}

}  // extern "C"
