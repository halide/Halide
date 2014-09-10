#include "runtime_internal.h"
#include "HalideRuntime.h"

extern "C" {

typedef void *GLXContext;
typedef void *GLXFBConfig;
typedef int Bool;
typedef void Display;

typedef void (*__GLXextFuncPtr)(void);
extern __GLXextFuncPtr glXGetProcAddressARB (const char *);
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
typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);

const char *strstr(const char *, const char *);

}  // extern "C"

namespace Halide { namespace Runtime { namespace Internal {

// Helper to check for extension string presence. Adapted from:
//   http://www.opengl.org/resources/features/OGLextensions/
bool extension_supported(const char *extlist, const char *extension) {
    // Extension names should not have spaces.
    if (strchr(extension, ' ') != NULL || *extension == '\0')
        return false;

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

}}}  // namespace Halide::Runtime::Internal


extern "C" {

WEAK void *halide_opengl_get_proc_address(void *user_context, const char *name) {
    return (void*)glXGetProcAddressARB(name);
}

// Initialize OpenGL
WEAK int halide_opengl_create_context(void *user_context) {
    if (glXGetCurrentContext()) {
        // Already have a context
        return 0;
    }

    void *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        halide_error(user_context, "Could not open X11 display.\n");
        return -1;
    }

    // GLX supported?
    if (!glXQueryExtension(dpy, NULL, NULL)) {
        halide_error(user_context, "GLX not supported by X server.\n");
        return -1;
    }

    int screen = XDefaultScreen(dpy);

    int attribs[] = {
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        0
    };
    int num_configs = 0;
    void** fbconfigs = glXChooseFBConfig(dpy, screen, attribs, &num_configs);
    if (!num_configs) {
        halide_error(user_context, "Could not get framebuffer config.\n");
        return -1;
    }

    void *fbconfig = fbconfigs[0];

    const char *glxexts = glXQueryExtensionsString(dpy, screen);

    // NOTE: It is not necessary to create or make current to a context before
    // calling glXGetProcAddressARB
    glXCreateContextAttribsARBProc glXCreateContextAttribsARB = 0;
    glXCreateContextAttribsARB = (glXCreateContextAttribsARBProc)
        glXGetProcAddressARB("glXCreateContextAttribsARB");

    void *context = 0;

    if (!extension_supported(glxexts, "GLX_ARB_create_context") ||
        !glXCreateContextAttribsARB) {
        context = glXCreateNewContext(dpy, fbconfig,
                                      GLX_RGBA_TYPE,
                                      NULL /* share list */, 1 /* direct */);
    } else {
        int desired_major_version = 3;
        int desired_minor_version = 2;
        // bool compatibility_profile = false;

        int context_attribs[] = {
            GLX_CONTEXT_MAJOR_VERSION_ARB, desired_major_version,
            GLX_CONTEXT_MINOR_VERSION_ARB, desired_minor_version,
            // TODO: compatibility?
            0
        };

        context = glXCreateContextAttribsARB(dpy, fbconfig, NULL /* */, 1 /* direct */,
                                             context_attribs);
//        XSync(dpy, 0 /* ??? */);
        if (context == 0) {
            halide_printf(user_context, "Opening 3.0 context failed, trying compatibility context\n");
            context_attribs[1] = 2;
            context_attribs[3] = 0;
            context = glXCreateContextAttribsARB(dpy, fbconfig, NULL /* */, 1 /* direct */,
                                                 context_attribs);

        }
        halide_printf(user_context, "Opened %d.%d context\n", context_attribs[1], context_attribs[3]);
    }

    if (!context) {
        halide_error(user_context, "Could not create OpenGL context.\n");
        return -1;
    }

    int pbuffer_attribs[] = {
        0x8041 /* GLX_PBUFFER_WIDTH */,  32,
        0x8040 /* GLX_PBUFFER_HEIGHT */, 32,
        0
    };
    unsigned long pbuffer = glXCreatePbuffer(dpy, fbconfig, pbuffer_attribs);

    // clean up:
    XFree(fbconfigs);
    XSync(dpy, 0);

    if (!glXMakeContextCurrent(dpy, pbuffer, pbuffer, context)) {
        halide_error(user_context, "Could not make context current.\n");
        return -1;
    }

    return 0;
}

}
