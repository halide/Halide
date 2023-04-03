#include "HalideRuntime.h"
#include "printer.h"

extern "C" {

typedef void *GLXContext;
typedef void *GLXFBConfig;
typedef int Bool;
typedef void Display;

typedef void (*__GLXextFuncPtr)();
extern __GLXextFuncPtr glXGetProcAddressARB(const char *);
extern void *XOpenDisplay(void *);
extern int XDefaultScreen(void *);
extern int glXQueryExtension(void *, void *, void *);
extern const char *glXQueryExtensionsString(Display *dpy, int screen);
extern GLXContext glXCreateNewContext(void *, void *, int, void *, int);
extern void **glXChooseFBConfig(void *, int, const int *, int *);
extern unsigned long glXCreatePbuffer(void *, void *, const int *);
extern int XFree(void *);
extern int XSync(void *, int);
extern GLXContext glXGetCurrentContext();
extern int glXMakeContextCurrent(void *, unsigned long, unsigned long, void *);

#define GLX_RENDER_TYPE 0x8011
#define GLX_RGBA_TYPE 0x8014
#define GLX_RGBA_BIT 1
#define GLX_RED_SIZE 8
#define GLX_GREEN_SIZE 8
#define GLX_BLUE_SIZE 8
#define GLX_ALPHA_SIZE 8

#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
typedef GLXContext (*glXCreateContextAttribsARBProc)(Display *, GLXFBConfig, GLXContext, Bool, const int *);

}  // extern "C"

namespace Halide {
namespace Runtime {
namespace Internal {

// Helper to check for extension string presence. Adapted from:
//   http://www.opengl.org/resources/features/OGLextensions/
WEAK bool glx_extension_supported(const char *extlist, const char *extension) {
    // Extension names should not have spaces.
    if (strchr(extension, ' ') != nullptr || *extension == '\0') {
        return false;
    }

    const char *start = extlist;
    while (const char *pos = strstr(start, extension)) {
        const char *end = pos + strlen(extension);
        // Ensure the found match is a full word, not a substring.
        if ((pos == start || pos[-1] == ' ') &&
            (*end == ' ' || *end == '\0')) {
            return true;
        }
        start = end;
    }
    return false;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

extern "C" {

WEAK void *halide_opengl_get_proc_address(void *user_context, const char *name) {
    return (void *)glXGetProcAddressARB(name);
}

// Initialize OpenGL
WEAK int halide_opengl_create_context(void *user_context) {
    const int desired_major_version = 3;
    const int desired_minor_version = 2;

    if (glXGetCurrentContext()) {
        // Already have a context
        return halide_error_code_success;
    }

    void *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        error(user_context) << "Could not open X11 display.";
        return halide_error_code_generic_error;
    }

    // GLX supported?
    if (!glXQueryExtension(dpy, nullptr, nullptr)) {
        error(user_context) << "GLX not supported by X server.";
        return halide_error_code_generic_error;
    }

    int screen = XDefaultScreen(dpy);

    int attribs[] = {
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        0};
    int num_configs = 0;
    void **fbconfigs = glXChooseFBConfig(dpy, screen, attribs, &num_configs);
    if (!num_configs) {
        error(user_context) << "Could not get framebuffer config.";
        return halide_error_code_generic_error;
    }
    void *fbconfig = fbconfigs[0];

    const char *glxexts = glXQueryExtensionsString(dpy, screen);
    void *share_list = nullptr;
    int direct = 1;
    void *context = nullptr;

    glXCreateContextAttribsARBProc glXCreateContextAttribsARB = nullptr;
    glXCreateContextAttribsARB = (glXCreateContextAttribsARBProc)
        glXGetProcAddressARB("glXCreateContextAttribsARB");

    if (glx_extension_supported(glxexts, "GLX_ARB_create_context") &&
        glXCreateContextAttribsARB) {
        int context_attribs[] = {
            GLX_CONTEXT_MAJOR_VERSION_ARB, desired_major_version,
            GLX_CONTEXT_MINOR_VERSION_ARB, desired_minor_version,
            0};
        context = glXCreateContextAttribsARB(dpy, fbconfig, share_list, direct,
                                             context_attribs);
    }
    if (!context) {
        // Open a legacy context
        context = glXCreateNewContext(dpy, fbconfig, GLX_RGBA_TYPE, share_list, direct);
    }
    if (!context) {
        error(user_context) << "Could not create OpenGL context.";
        return halide_error_code_generic_error;
    }

    int pbuffer_attribs[] = {
        0x8041 /* GLX_PBUFFER_WIDTH */, 32,
        0x8040 /* GLX_PBUFFER_HEIGHT */, 32,
        0};
    unsigned long pbuffer = glXCreatePbuffer(dpy, fbconfig, pbuffer_attribs);

    XFree(fbconfigs);
    XSync(dpy, 0);

    if (!glXMakeContextCurrent(dpy, pbuffer, pbuffer, context)) {
        error(user_context) << "Could not make context current.";
        return halide_error_code_generic_error;
    }

    return halide_error_code_success;
}
}
