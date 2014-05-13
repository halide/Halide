#if defined(unix) || defined(__unix__) || defined(__unix)

#include "GL/glx.h"
#include <stdio.h>

extern "C" void *halide_opengl_get_proc_address(const char *name) {
    return (void *)glXGetProcAddressARB((const GLubyte *)name);
}

// Initialize OpenGL
extern "C" int halide_opengl_create_context() {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Could not open X11 display.\n");
        return 1;
    }

    // GLX supported?
    if (!glXQueryExtension(dpy, NULL, NULL)) {
        fprintf(stderr, "GLX not supported by X server.\n");
        return 1;
    }

    int attribs[] = {GLX_RGBA, None};
    XVisualInfo *vi = glXChooseVisual(dpy, DefaultScreen(dpy), attribs);
    if (!vi) {
        fprintf(stderr, "Could not find suitable visual.\n");
        return 1;
    }

    GLXContext ctx = glXCreateContext(dpy, vi, None, True);
    if (!ctx) {
        fprintf(stderr, "Could not create OpenGL context.\n");
        return 1;
    }

    Colormap cmap = 0;
    cmap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual,
                           AllocNone);
    XSetWindowAttributes window_attribs;
    window_attribs.border_pixel = 0;
    window_attribs.colormap = cmap;
    Window wnd = XCreateWindow(dpy, RootWindow(dpy, vi->screen), 0, 0, 1, 1, 0,
                               vi->depth, InputOutput, vi->visual,
                               CWBorderPixel | CWColormap, &window_attribs);

    if (!glXMakeCurrent(dpy, wnd, ctx)) {
        fprintf(stderr, "Could not activate OpenGL context.\n");
        return 1;
    }
    return 0;
}
#elif defined(__APPLE__)

#include <AGL/agl.h>
#include <dlfcn.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

extern "C" void *halide_opengl_get_proc_address(const char *name) {
    static void *dylib = NULL;
    if (!dylib) {
        dylib = dlopen(
            "/System/Library/Frameworks/OpenGL.framework/Versions/Current/"
            "OpenGL",
            RTLD_LAZY);
        if (!dylib) return NULL;
    }
    return dlsym(dylib, name);

    // char *symbolName = (char*) malloc(strlen(name) + 2);
    // strcpy(symbolName + 1, name);
    // symbolName[0] = '_';
    // NSSymbol symbol = NULL;
    // if (NSIsSymbolNameDefined(symbolName))
    //     symbol = NSLookupAndBindSymbol(symbolName);
    // free(symbolName);
    // return symbol ? NSAddressOfSymbol (symbol) : NULL;
}

// Initialize OpenGL
extern "C" int halide_opengl_create_context() {
    AGLContext ctx = NULL;

    int attrib[] = {AGL_RGBA, AGL_NONE};
    AGLPixelFormat pf = aglChoosePixelFormat(NULL, 0, attrib);
    if (!pf) {
        fprintf(stderr, "Could not create pixel format\n");
        return 1;
    }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx || aglGetError() != AGL_NO_ERROR) {
        fprintf(stderr, "Could not create context\n");
        return 1;
    }
    aglDestroyPixelFormat(pf);
    if (aglSetCurrentContext(ctx) == GL_FALSE) {
        fprintf(stderr, "Could not activate OpenGL context\n");
        return 1;
    }
    return 0;
}
#pragma clang diagnostic pop

#else
#error "Unsupported platform"
#endif
