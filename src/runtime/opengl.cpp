#include "runtime_internal.h"
#include "device_interface.h"
#include "../buffer_t.h"
#include "HalideRuntimeOpenGL.h"

#include "mini_opengl.h"

// This constant is used to indicate that the application will take
// responsibility for binding the output render target before calling the
// Halide function.
#define HALIDE_OPENGL_CLIENT_BOUND ((uint64_t)-1)

// Implementation note: all function that directly or indirectly access the
// runtime state in halide_opengl_state must be declared as WEAK, otherwise
// the behavior at runtime is undefined.

// List of all OpenGL functions used by the runtime. The list is used to
// declare and initialize the dispatch table in OpenGLState below.
#define USED_GL_FUNCTIONS                                               \
    GLFUNC(PFNGLDELETETEXTURESPROC, DeleteTextures);                    \
    GLFUNC(PFNGLGENTEXTURESPROC, GenTextures);                          \
    GLFUNC(PFNGLBINDTEXTUREPROC, BindTexture);                          \
    GLFUNC(PFNGLGETERRORPROC, GetError);                                \
    GLFUNC(PFNGLVIEWPORTPROC, Viewport);                                \
    GLFUNC(PFNGLGENBUFFERSPROC, GenBuffers);                            \
    GLFUNC(PFNGLDELETEBUFFERSPROC, DeleteBuffers);                      \
    GLFUNC(PFNGLBINDBUFFERPROC, BindBuffer);                            \
    GLFUNC(PFNGLBUFFERDATAPROC, BufferData);                            \
    GLFUNC(PFNGLTEXPARAMETERIPROC, TexParameteri);                      \
    GLFUNC(PFNGLTEXIMAGE2DPROC, TexImage2D);                            \
    GLFUNC(PFNGLTEXSUBIMAGE2DPROC, TexSubImage2D);                      \
    GLFUNC(PFNGLDISABLEPROC, Disable);                                  \
    GLFUNC(PFNGLCREATESHADERPROC, CreateShader);                        \
    GLFUNC(PFNGLACTIVETEXTUREPROC, ActiveTexture);                      \
    GLFUNC(PFNGLSHADERSOURCEPROC, ShaderSource);                        \
    GLFUNC(PFNGLCOMPILESHADERPROC, CompileShader);                      \
    GLFUNC(PFNGLGETSHADERIVPROC, GetShaderiv);                          \
    GLFUNC(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog);                \
    GLFUNC(PFNGLDELETESHADERPROC, DeleteShader);                        \
    GLFUNC(PFNGLCREATEPROGRAMPROC, CreateProgram);                      \
    GLFUNC(PFNGLATTACHSHADERPROC, AttachShader);                        \
    GLFUNC(PFNGLLINKPROGRAMPROC, LinkProgram);                          \
    GLFUNC(PFNGLGETPROGRAMIVPROC, GetProgramiv);                        \
    GLFUNC(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog);              \
    GLFUNC(PFNGLUSEPROGRAMPROC, UseProgram);                            \
    GLFUNC(PFNGLDELETEPROGRAMPROC, DeleteProgram);                      \
    GLFUNC(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation);            \
    GLFUNC(PFNGLUNIFORM1IVPROC, Uniform1iv);                            \
    GLFUNC(PFNGLUNIFORM2IVPROC, Uniform2iv);                            \
    GLFUNC(PFNGLUNIFORM1FVPROC, Uniform1fv);                            \
    GLFUNC(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers);                  \
    GLFUNC(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers);            \
    GLFUNC(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus);    \
    GLFUNC(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer);                  \
    GLFUNC(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D);        \
    GLFUNC(PFNGLGETATTRIBLOCATIONPROC, GetAttribLocation);              \
    GLFUNC(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer);          \
    GLFUNC(PFNGLDRAWELEMENTSPROC, DrawElements);                        \
    GLFUNC(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray);  \
    GLFUNC(PFNGLDISABLEVERTEXATTRIBARRAYPROC, DisableVertexAttribArray); \
    GLFUNC(PFNGLPIXELSTOREIPROC, PixelStorei);                          \
    GLFUNC(PFNGLREADPIXELS, ReadPixels);                                \
    GLFUNC(PFNGLGETSTRINGPROC, GetString);                              \
    GLFUNC(PFNGLGETINTEGERV, GetIntegerv);                              \
    GLFUNC(PFNGLGETSTRINGI, GetStringi)

// ---------- Types ----------

using namespace Halide::Runtime::Internal;

namespace Halide { namespace Runtime { namespace Internal { namespace OpenGL {

extern WEAK halide_device_interface opengl_device_interface;

enum OpenGLProfile {
    OpenGL,
    OpenGLES
};

struct Argument {
    // The kind of data stored in an argument
    enum Kind {
        Invalid,
        Var,                            // uniform variable
        Inbuf,                          // input texture
        Outbuf                          // output texture
    };

    // The elementary data type of the argument
    enum Type {
        Void, Bool, Float, Int8, Int16, Int32, UInt8, UInt16, UInt32
    };

    char *name;
    Kind kind;
    Type type;
    Argument *next;
};

struct KernelInfo {
    char *source;
    Argument *arguments;
    GLuint shader_id;
    GLuint program_id;
};

// Information about each known texture.
struct TextureInfo {
    GLuint id;
    GLint min[4];
    GLint extent[4];
    bool halide_allocated;              // allocated by us or host app?
    TextureInfo *next;
};

struct ModuleState {
    KernelInfo *kernel;
    ModuleState *next;
};

// All persistent state maintained by the runtime.
struct GlobalState {
    void init();

    bool initialized;

    // Information about the OpenGL platform we're running on.
    OpenGLProfile profile;
    int major_version, minor_version;
    bool have_vertex_array_objects;
    bool have_texture_rg;
    bool have_texture_float;

    // Various objects shared by all filter kernels
    GLuint vertex_shader_id;
    GLuint framebuffer_id;
    GLuint vertex_array_object;
    GLuint vertex_buffer;
    GLuint element_buffer;

    // A list of all textures that are still active
    TextureInfo *textures;

    // Declare pointers used OpenGL functions
#define GLFUNC(PTYPE,VAR) PTYPE VAR
    USED_GL_FUNCTIONS;
#undef GLFUNC
    PFNGLGENVERTEXARRAYS GenVertexArrays;
    PFNGLBINDVERTEXARRAY BindVertexArray;
    PFNGLDELETEVERTEXARRAYS DeleteVertexArrays;
};


WEAK GlobalState global_state;

// A list of module-specific state. Each module corresponds to a single Halide filter
WEAK ModuleState *state_list;

WEAK const char *vertex_shader_src =
    "attribute vec2 position;\n"
    "varying vec2 pixcoord;\n"
    "uniform ivec2 output_min;\n"
    "uniform ivec2 output_extent;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "    vec2 texcoord = 0.5 * position + 0.5;\n"
    "    pixcoord = texcoord * vec2(output_extent.xy) + vec2(output_min.xy);\n"
    "}\n";

WEAK const char *kernel_marker = "/// KERNEL ";
WEAK const char *input_marker  = "/// IN_BUFFER ";
WEAK const char *output_marker = "/// OUT_BUFFER ";
WEAK const char *var_marker    = "/// VAR ";

// Ensure that OpenGL runtime is correctly initialized. Used in all public API
// functions.
#define CHECK_INITIALIZED(ERRORCODE)                            \
    if (!global_state.initialized) {                                      \
        error(user_context) << "OpenGL runtime not initialized.";   \
        return ERRORCODE;                                       \
    }

// Macro for error checking.
#ifdef DEBUG_RUNTIME
#define LOG_GLERROR(ERR)                                        \
    error(user_context) << __FILE__ << ":" << __LINE__ << ": OpenGL error " << (ERR);
#else
#define LOG_GLERROR(ERR)
#endif

#define CHECK_GLERROR(ERRORCODE) do {                                   \
        GLenum err = global_state.GetError();                           \
        if (err != GL_NO_ERROR) {                                       \
            LOG_GLERROR(err);                                           \
            error(user_context) << "OpenGL error";                      \
            return ERRORCODE;                                           \
        }} while (0)


// ---------- Helper functions ----------

WEAK char *strndup(const char *s, size_t n) {
    char *p = (char*)malloc(n+1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

WEAK GLuint get_texture_id(buffer_t *buf) {
    if (buf->dev == 0) {
        return 0;
    } else {
        return get_device_handle(buf->dev) & 0xffffffff;
    }
}

WEAK void debug_buffer(void *user_context, buffer_t *buf) {
    debug(user_context)
        << "  dev: " << buf->dev << "\n"
        << "  texture_id: " << get_texture_id(buf) << "\n"
        << "  host: " << buf->host << "\n"
        << "  extent: " << buf->extent[0] << " " << buf->extent[1]
        << " " << buf->extent[2] << " " << buf->extent[3] <<  "\n"
        << "  stride: " << buf->stride[0] << " " << buf->stride[1]
        << " " << buf->stride[2] << " " << buf->stride[3] <<  "\n"
        << "  min: " << buf->min[0] << " " << buf->min[1]
        << " " << buf->min[2] << " " << buf->min[3] <<  "\n"
        << "  elem_size: " << buf->elem_size << "\n"
        << "  host_dirty: " << buf->host_dirty << "\n"
        << "  dev_dirty: " << buf->dev_dirty << "\n";
}

WEAK GLuint make_shader(void *user_context, GLenum type,
                        const char *source, GLint *length) {
    GLuint shader = global_state.CreateShader(type);
    CHECK_GLERROR(1);
    global_state.ShaderSource(shader, 1, (const GLchar **)&source, length);
    CHECK_GLERROR(1);
    global_state.CompileShader(shader);
    CHECK_GLERROR(1);

    GLint shader_ok = 0;
    global_state.GetShaderiv(shader, GL_COMPILE_STATUS, &shader_ok);
    if (!shader_ok) {
        print(user_context) << "Could not compile shader:\n";
        GLint log_len;
        global_state.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        char *log = (char *)malloc(log_len);
        global_state.GetShaderInfoLog(shader, log_len, NULL, log);
        print(user_context) << log << "\n";
        free(log);
        global_state.DeleteShader(shader);
        return 0;
    }
    return shader;
}

// Check whether string starts with a given prefix.
// Returns pointer to character after matched prefix if successful or NULL.
WEAK const char *match_prefix(const char *s, const char *prefix) {
    if (0 == strncmp(s, prefix, strlen(prefix))) {
        return s + strlen(prefix);
    }
    return NULL;
}

// Parse declaration of the form "type name" and construct matching Argument.
WEAK Argument *parse_argument(void *user_context, const char *src,
                              const char *end) {
    const char *name;
    Argument::Type type = Argument::Void;
    if ((name = match_prefix(src, "float "))) {
        type = Argument::Float;
    } else if ((name = match_prefix(src, "bool "))) {
        type = Argument::Bool;
    } else if ((name = match_prefix(src, "int8_t "))) {
        type = Argument::Int8;
    } else if ((name = match_prefix(src, "int16_t "))) {
        type = Argument::Int16;
    } else if ((name = match_prefix(src, "int32_t "))) {
        type = Argument::Int32;
    } else if ((name = match_prefix(src, "uint8_t "))) {
        type = Argument::UInt8;
    } else if ((name = match_prefix(src, "uint16_t "))) {
        type = Argument::UInt16;
    } else if ((name = match_prefix(src, "uint32_t "))) {
        type = Argument::UInt32;
    }
    if (type == Argument::Void) {
        error(user_context) << "Internal error: argument type not supported";
        return NULL;
    }

    Argument *arg = (Argument *)malloc(sizeof(Argument));
    arg->name = strndup(name, end - name);
    arg->type = type;
    arg->kind = Argument::Invalid;
    arg->next = 0;
    return arg;
}

// Create KernelInfo for a piece of GLSL code
WEAK KernelInfo *create_kernel(void *user_context, const char *src, int size) {
    KernelInfo *kernel = (KernelInfo *)malloc(sizeof(KernelInfo));

    kernel->source = strndup(src, size);
    kernel->arguments = NULL;
    kernel->shader_id = 0;
    kernel->program_id = 0;

    #ifdef DEBUG_RUNTIME
    {
        // Android logcat output clips at ~1000 character chunks by default;
        // to avoid clipping the interesting stuff, emit a line at a time.
        // This is less efficient, but it's DEBUG-only.
        debug(user_context) << "Compiling GLSL kernel (size = " << size << "):\n";
        const int kBufSize = 255;
        char buf[kBufSize + 1];
        const char* s = src;
        int d = 0;
        while (s < src + size) {
            while (*s != '\n' && *s != '\0' && d < kBufSize) {
                buf[d++] = *s++;
            }
            buf[d++] = '\0';
            debug(user_context) << buf << "\n";
            d = 0;
            while (*s == '\n' || *s == '\0') {
                s++;
            }
        }
    }
    #endif

    // Parse initial comment block
    const char *line = kernel->source;
    while (*line) {
        const char *next_line = strchr(line, '\n') + 1;
        if (!next_line)
            next_line = line + size;

        const char *args;
        if ((args = match_prefix(line, kernel_marker))) {
            // ignore
        } else if ((args = match_prefix(line, var_marker))) {
            if (Argument *arg = parse_argument(user_context, args, next_line - 1)) {
                arg->kind = Argument::Var;
                arg->next = kernel->arguments;
                kernel->arguments = arg;
            } else {
                error(user_context) << "Invalid VAR marker";
                goto error;
            }
        } else if ((args = match_prefix(line, input_marker))) {
            if (Argument *arg = parse_argument(user_context, args, next_line - 1)) {
                arg->kind = Argument::Inbuf;
                arg->next = kernel->arguments;
                kernel->arguments = arg;
            } else {
                error(user_context) << "Invalid IN_BUFFER marker";
                goto error;
            }
        } else if ((args = match_prefix(line, output_marker))) {
            if (Argument *arg = parse_argument(user_context, args, next_line - 1)) {
                arg->kind = Argument::Outbuf;
                arg->next = kernel->arguments;
                kernel->arguments = arg;
            } else {
                error(user_context) << "Invalid OUT_BUFFER marker";
                goto error;
            }
        } else {
            // Stop parsing if we encounter something we don't recognize
            break;
        }
        line = next_line;
    }

    // Arguments are currently in reverse order, flip the list.
    {
        Argument *cur = kernel->arguments;
        kernel->arguments = NULL;
        while (cur) {
            Argument *next = cur->next;
            cur->next = kernel->arguments;
            kernel->arguments = cur;
            cur = next;
        }
    }

    return kernel;
  error:
    free(kernel);
    return NULL;
}

// Delete all data associated with a kernel. Also release associated OpenGL
// shader and program.
WEAK void delete_kernel(void *user_context, KernelInfo *kernel) {
    global_state.DeleteProgram(kernel->program_id);
    global_state.DeleteShader(kernel->shader_id);

    Argument *arg = kernel->arguments;
    while (arg) {
        Argument *next = arg->next;
        free(arg->name);
        free(arg);
        arg = next;
    }
    free(kernel->source);
    free(kernel);
}

// Vertices and their order in a triangle strip for rendering a quad
// ranging from (-1,-1) to (1,1).
WEAK GLfloat quad_vertices[] = {
    -1.0f, -1.0f,    1.0f, -1.0f,
    -1.0f,  1.0f,    1.0f,  1.0f
};
WEAK GLuint quad_indices[] = { 0, 1, 2, 3 };

WEAK void GlobalState::init() {
    initialized = false;
    profile = OpenGL;
    major_version = 2;
    minor_version = 0;
    vertex_shader_id = 0;
    framebuffer_id = 0;
    vertex_array_object = vertex_buffer = element_buffer = 0;
    textures = NULL;
    have_vertex_array_objects = false;
    have_texture_rg = false;
}

WEAK int load_gl_func(void *user_context, const char *name, void **ptr) {
    void *p = halide_opengl_get_proc_address(user_context, name);
    if (!p) {
        error(user_context) << "Could not load function pointer for " << name;
        return -1;
    }
    *ptr = p;
    return 0;
}

WEAK bool extension_supported(void *user_context, const char *name) {
    if (global_state.major_version >= 3) {
        GLint num_extensions = 0;
        global_state.GetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
        for (int i = 0; i < num_extensions; i++) {
            const char *ext = (const char *)global_state.GetStringi(GL_EXTENSIONS, i);
            if (strcmp(ext, name) == 0) {
                return true;
            }
        }
    } else {
        const char *start = (const char *)global_state.GetString(GL_EXTENSIONS);
        if (!start) {
            return false;
        }
        while (const char *pos = strstr(start, name)) {
            const char *end = pos + strlen(name);
            // Ensure the found match is a full word, not a substring.
            if ((pos == start || pos[-1] == ' ') &&
                (*end == ' ' || *end == '\0')) {
                return true;
            }
            start = end;
        }
    }
    return false;
}

// Check for availability of various version- and extension-specific features
// and hook up functions pointers as necessary
WEAK void init_extensions(void *user_context) {
    if (global_state.major_version >= 3) {
        global_state.have_vertex_array_objects = true;
        load_gl_func(user_context, "glGenVertexArrays", (void**)&global_state.GenVertexArrays);
        load_gl_func(user_context, "glBindVertexArray", (void**)&global_state.BindVertexArray);
        load_gl_func(user_context, "glDeleteVertexArrays", (void**)&global_state.DeleteVertexArrays);
    }

    global_state.have_texture_rg =
        global_state.major_version >= 3 ||
        (global_state.profile == OpenGL &&
         extension_supported(user_context, "GL_ARB_texture_rg")) ||
        (global_state.profile == OpenGLES &&
         extension_supported(user_context, "GL_EXT_texture_rg"));

    global_state.have_texture_float =
        (global_state.major_version >= 3) ||
        (global_state.profile == OpenGL &&
         extension_supported(user_context, "GL_ARB_texture_float")) ||
        (global_state.profile == OpenGLES &&
         extension_supported(user_context, "GL_OES_texture_float"));
}

WEAK const char *parse_int(const char *str, int *val) {
    int v = 0;
    size_t i = 0;
    while (str[i] >= '0' && str[i] <= '9') {
        v = 10 * v + (str[i] - '0');
        i++;
    }
    if (i > 0) {
        *val = v;
        return &str[i];
    }
    return NULL;
}

WEAK const char *parse_opengl_version(const char *str, int *major, int *minor) {
    str = parse_int(str, major);
    if (str == NULL || *str != '.') {
        return NULL;
    }
    return parse_int(str + 1, minor);
}

// Initialize the OpenGL-specific parts of the runtime.
WEAK int halide_opengl_init(void *user_context) {
    if (global_state.initialized) {
        return 0;
    }

    global_state.init();

    // Make a context if there isn't one
    if (halide_opengl_create_context(user_context)) {
        error(user_context) << "Failed to make OpenGL context";
        return -1;
    }

    // Initialize pointers to core OpenGL functions.
#define GLFUNC(TYPE, VAR)                                               \
    if (load_gl_func(user_context, "gl" #VAR, (void**)&global_state.VAR) < 0) {   \
        return -1;                                                      \
    }
    USED_GL_FUNCTIONS;
#undef GLFUNC

    const char *version = (const char *)global_state.GetString(GL_VERSION);
    const char *gles_version = match_prefix(version, "OpenGL ES ");
    int major, minor;
    if (gles_version && parse_opengl_version(gles_version, &major, &minor)) {
        global_state.profile = OpenGLES;
        global_state.major_version = major;
        global_state.minor_version = minor;
    } else if (parse_opengl_version(version, &major, &minor)) {
        global_state.profile = OpenGL;
        global_state.major_version = major;
        global_state.minor_version = minor;
    } else {
        global_state.profile = OpenGL;
        global_state.major_version = 2;
        global_state.minor_version = 0;
    }
    init_extensions(user_context);
    debug(user_context)
        << "Halide running on OpenGL "
        << ((global_state.profile == OpenGL) ? "" : "ES ")
        << major << "." << minor << "\n"
        << "  vertex_array_objects: "
        << (global_state.have_vertex_array_objects ? "yes\n" : "no\n")
        << "  texture_rg: "
        << (global_state.have_texture_rg ? "yes\n" : "no\n")
        << "  texture_float: "
        << (global_state.have_texture_float ? "yes\n" : "no\n");

    // Initialize framebuffer.
    global_state.GenFramebuffers(1, &global_state.framebuffer_id);
    CHECK_GLERROR(1);

    // Initialize vertex shader.
    global_state.vertex_shader_id = make_shader(user_context, GL_VERTEX_SHADER,
                                      vertex_shader_src, NULL);
    if (global_state.vertex_shader_id == 0) {
        error(user_context) << "Failed to create vertex shader";
        return 1;
    }

    // Initialize vertex and element buffers.
    GLuint buf[2];
    global_state.GenBuffers(2, buf);
    global_state.BindBuffer(GL_ARRAY_BUFFER, buf[0]);
    global_state.BufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
    global_state.BindBuffer(GL_ARRAY_BUFFER, 0);
    global_state.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf[1]);
    global_state.BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quad_indices), quad_indices, GL_STATIC_DRAW);
    global_state.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    global_state.vertex_buffer = buf[0];
    global_state.element_buffer = buf[1];

    if (global_state.have_vertex_array_objects) {
        global_state.GenVertexArrays(1, &global_state.vertex_array_object);
    }

    CHECK_GLERROR(1);
    global_state.initialized = true;
    return 0;
}

// Release all data allocated by the runtime.
//
// The OpenGL context itself is generally managed by the host application, so
// we leave it untouched.
WEAK int halide_opengl_device_release(void *user_context) {
    if (!global_state.initialized) return 0;

    debug(user_context) << "halide_opengl_release\n";
    global_state.DeleteShader(global_state.vertex_shader_id);
    global_state.DeleteFramebuffers(1, &global_state.framebuffer_id);

    ModuleState *mod = state_list;
    while (mod) {
        delete_kernel(user_context, mod->kernel);
        mod->kernel = NULL;
        ModuleState *next = mod->next;
        // do not call free(mod) to avoid dangling pointers: the module state
        // is still referenced in the code generated by Halide (see
        // CodeGen_GPU_Host::get_module_state).
        mod = next;
    }

    // Delete all textures that were allocated by us.
    TextureInfo *tex = global_state.textures;
    int freed_textures = 0;
    while (tex) {
        TextureInfo *next = tex->next;
        if (tex->halide_allocated) {
            global_state.DeleteTextures(1, &tex->id);
            CHECK_GLERROR(1);
            freed_textures++;
        }
        free(tex);
        tex = next;
    }

    if (freed_textures > 0) {
        debug(user_context) << "halide_opengl_release: deleted "
                            << freed_textures << " dangling texture(s).\n";
    }

    global_state.DeleteBuffers(1, &global_state.vertex_buffer);
    global_state.DeleteBuffers(1, &global_state.element_buffer);
    if (global_state.have_vertex_array_objects) {
        global_state.DeleteVertexArrays(1, &global_state.vertex_array_object);
    }

    global_state = GlobalState();

    return 0;
}

// Determine OpenGL texture format and channel type for a given buffer_t.
WEAK bool get_texture_format(void *user_context, buffer_t *buf,
                             GLint *internal_format, GLint *format, GLint *type) {
    if (buf->elem_size == 1) {
        *type = GL_UNSIGNED_BYTE;
    } else if (buf->elem_size == 2) {
        *type = GL_UNSIGNED_SHORT;
    } else if (buf->elem_size == 4) {
        *type = GL_FLOAT;
    } else {
        error(user_context) << "OpenGL: Only uint8, uint16, and float textures are supported.";
        return false;
    }

    const int channels = buf->extent[2];
    if (channels <= 2 && !global_state.have_texture_rg) {
        error(user_context) << "OpenGL: This version of OpenGL doesn't support <=2 channels.";
        return false;
    }
    if (channels == 1) {
        *format = GL_RED;
    } else if (channels == 2) {
        *format = GL_RG;
    } else if (channels == 3) {
        *format = GL_RGB;
    } else if (channels == 4) {
        *format = GL_RGBA;
    } else {
        error(user_context) << "OpenGL: Only 3 or 4 color channels are supported.";
        return false;
    }

    switch (global_state.profile) {
    case OpenGLES:
        // For OpenGL ES, the texture format has to match the pixel format
        // since there no conversion is performed during texture transfers.
        // See OES_texture_float.
        *internal_format = *format;
        break;
    case OpenGL:
        // For desktop OpenGL, the internal format specifiers include the
        // precise data type, see ARB_texture_float.
        if (*type == GL_FLOAT) {
            switch (*format) {
            case GL_RED: *internal_format = GL_R32F; break;
            case GL_RG: *internal_format = GL_RG32F; break;
            case GL_RGB: *internal_format = GL_RGB32F; break;
            case GL_RGBA: *internal_format = GL_RGBA32F; break;
            }
        } else {
            *internal_format = *format;
        }
        break;
    }

    return true;
}


WEAK TextureInfo *find_texture(GLuint tex) {
    TextureInfo *texinfo = global_state.textures;
    while (texinfo && texinfo->id != tex) {
        texinfo = texinfo->next;
    }
    return texinfo;
}

// Allocate a new texture matching the dimension and color format of the
// specified buffer.
WEAK int halide_opengl_device_malloc(void *user_context, buffer_t *buf) {
    if (int error = halide_opengl_init(user_context)) {
        return error;
    }

    if (!buf) {
        error(user_context) << "Invalid buffer";
        return 1;
    }

    // If the texture was already created by the host application, check that
    // it has the correct format. Otherwise, allocate and set up an
    // appropriate texture.
    GLuint tex = get_texture_id(buf);
    bool halide_allocated = false;
    GLint width, height;
    if (tex != 0) {
#ifdef HAVE_GLES3
        global_state.BindTexture(GL_TEXTURE_2D, tex);
        global_state.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        global_state.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        CHECK_GLERROR(1);
        if (width < buf->extent[0] || height < buf->extent[1]) {

            error(user_context)
                << "Existing texture is smaller than buffer. "
                << "Texture size: " << width << "x" << height
                << ", buffer size: " << buf->extent[0] << "x" << buf->extent[1];

            return 1;
        }
#endif
    } else {
        if (buf->extent[3] > 1) {
            error(user_context) << "3D textures are not supported";
            return 1;
        }

        // Generate texture ID
        global_state.GenTextures(1, &tex);
        CHECK_GLERROR(1);

        // Set parameters for this texture: no interpolation and clamp to edges.
        global_state.BindTexture(GL_TEXTURE_2D, tex);
        global_state.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        global_state.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        global_state.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        global_state.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        CHECK_GLERROR(1);

        // Create empty texture here and fill it with glTexSubImage2D later.
        GLint internal_format = 0;
        GLint format = 0;
        GLint type = GL_UNSIGNED_BYTE;
        if (!get_texture_format(user_context, buf, &internal_format, &format, &type)) {
            error(user_context) << "Invalid texture format";
            return 1;
        }
        width = buf->extent[0];
        height = buf->extent[1];
        global_state.TexImage2D(GL_TEXTURE_2D, 0, internal_format,
                      width, height, 0, format, type, NULL);
        CHECK_GLERROR(1);

        buf->dev = new_device_wrapper(tex, &opengl_device_interface);
        if (buf->dev == 0) {
            error(user_context) << "OpenGL: out of memory allocating device wrapper.\n";
            global_state.DeleteTextures(1, &tex);
            return -1;
        }
        halide_allocated = true;
        debug(user_context) << "Allocated texture " << tex
                            << " of size " << width << " x " << height << "\n";

        global_state.BindTexture(GL_TEXTURE_2D, 0);
    }

    // Record main information about texture and remember it for later. In
    // halide_opengl_run we are only given the texture ID and not the full
    // buffer_t, so we copy the interesting information here.  Note: there can
    // be multiple dev_malloc calls for the same buffer_t; only record texture
    // information once.
    if (!find_texture(tex)) {
        TextureInfo *texinfo = (TextureInfo*)malloc(sizeof(TextureInfo));
        texinfo->id = tex;
        for (int i=0; i<3; i++) {
            texinfo->min[i] = buf->min[i];
            texinfo->extent[i] = buf->extent[i];
        }
        texinfo->halide_allocated = halide_allocated;

        texinfo->next = global_state.textures;
        global_state.textures = texinfo;
    }
    return 0;
}

// Delete all texture information associated with a buffer. The OpenGL texture
// itself is only deleted if it was actually allocated by Halide and not
// provided by the host application.
WEAK int halide_opengl_device_free(void *user_context, buffer_t *buf) {
    if (!global_state.initialized) {
        error(user_context) << "OpenGL runtime not initialized in call to halide_opengl_device_free.";
        return 1;
    }

    GLuint tex = get_texture_id(buf);
    if (tex == 0) {
        return 0;
    }

    // Look up corresponding TextureInfo and unlink it from the list.
    TextureInfo **ptr = &global_state.textures;
    TextureInfo *texinfo = *ptr;
    for (; texinfo != NULL; ptr = &texinfo->next, texinfo = *ptr) {
        if (texinfo->id == tex) {
            *ptr = texinfo->next;
            texinfo->next = NULL;
            break;
        }
    }
    if (!texinfo) {
        error(user_context) << "Internal error: texture " << tex << " not found.";
        return 1;
    }

    // Delete texture if it was allocated by us.
    if (texinfo->halide_allocated) {
        debug(user_context) << "Deleting texture " << tex << "\n";
        global_state.DeleteTextures(1, &tex);
        CHECK_GLERROR(1);
        delete_device_wrapper(buf->dev);
        buf->dev = 0;
    }

    free(texinfo);
    return 0;
}

WEAK int halide_opengl_dev_sync(void *user_context) {
    CHECK_INITIALIZED(1);
    // TODO: glFinish()
    return 0;
}

template <class T>
WEAK void halide_to_interleaved(buffer_t *buf, T *dst, int width, int height, int channels) {
    T *src = reinterpret_cast<T *>(buf->host);
    for (int y = 0; y < height; y++) {
        int dstidx = y * width * channels;
        for (int x = 0; x < width; x++) {
            int srcidx = (y - buf->min[1]) * buf->stride[1] +
                         (x - buf->min[0]) * buf->stride[0] +
                         -buf->min[2] * buf->stride[2];
            for (int c = 0; c < channels; c++) {
                dst[dstidx] = src[srcidx];
                dstidx++;
                srcidx += buf->stride[2];
            }
        }
    }
}

template <class T>
WEAK void interleaved_to_halide(buffer_t *buf, T *src, int width, int height, int channels) {
    T *dst = reinterpret_cast<T *>(buf->host);
    for (int y = 0; y < height; y++) {
        int srcidx = y * width * channels;
        for (int x = 0; x < width; x++) {
            int dstidx = (y - buf->min[1]) * buf->stride[1] +
                         (x - buf->min[0]) * buf->stride[0] +
                         -buf->min[2] * buf->stride[2];
            for (int c = 0; c < channels; c++) {
                dst[dstidx] = src[srcidx];
                srcidx++;
                dstidx += buf->stride[2];
            }
        }
    }
}

// Copy image data from host memory to texture.
WEAK int halide_opengl_copy_to_device(void *user_context, buffer_t *buf) {
    if (!global_state.initialized) {
        error(user_context) << "OpenGL runtime not initialized (halide_opengl_copy_to_device).";
        return 1;
    }

    int err = halide_opengl_device_malloc(user_context, buf);
    if (err) {
        return err;
    }

    if (!buf->host_dirty) {
        return 0;
    }

    if (!buf->host || !buf->dev) {
        debug_buffer(user_context, buf);
        error(user_context) << "Invalid copy_to_device operation: host or dev NULL";
        return 1;
    }

    GLuint tex = get_texture_id(buf);
    debug(user_context) << "halide_copy_to_device: " << tex << "\n";

    global_state.BindTexture(GL_TEXTURE_2D, tex);
    CHECK_GLERROR(1);

    GLint internal_format, format, type;
    if (!get_texture_format(user_context, buf, &internal_format, &format, &type)) {
        error(user_context) << "Invalid texture format";
        return 1;
    }
    GLint width = buf->extent[0];
    GLint height = buf->extent[1];

    // To use TexSubImage2D directly, the colors must be stored interleaved
    // and rows must be stored consecutively.
    bool is_interleaved = (buf->stride[2] == 1 && buf->stride[0] == buf->extent[2]);
    bool is_packed = (buf->stride[1] == buf->extent[0] * buf->stride[0]);
    if (is_interleaved && is_packed) {
        global_state.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        uint8_t *host_ptr = buf->host + buf->elem_size *
            (buf->min[0] * buf->stride[0] +
             buf->min[1] * buf->stride[1] +
             buf->min[2] * buf->stride[2] +
             buf->min[3] * buf->stride[3]);
        global_state.TexSubImage2D(GL_TEXTURE_2D, 0,
                         0, 0, width, height,
                         format, type, host_ptr);
        CHECK_GLERROR(1);
    } else {
        debug(user_context)
            << "Warning: In copy_to_device, host buffer is not interleaved. Doing slow interleave.\n";

        size_t size = width * height * buf->extent[2] * buf->elem_size;
        void *tmp = halide_malloc(user_context, size);

        switch (type) {
        case GL_UNSIGNED_BYTE:
            halide_to_interleaved<uint8_t>(buf, (uint8_t*)tmp, width, height, buf->extent[2]);
            break;
        case GL_UNSIGNED_SHORT:
            halide_to_interleaved<uint16_t>(buf, (uint16_t*)tmp, width, height, buf->extent[2]);
            break;
        case GL_FLOAT:
            halide_to_interleaved<float>(buf, (float*)tmp, width, height, buf->extent[2]);
            break;
        }

        global_state.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        global_state.TexSubImage2D(GL_TEXTURE_2D, 0,
                         0, 0, width, height,
                         format, type, tmp);
        CHECK_GLERROR(1);

        halide_free(user_context, tmp);
    }
    global_state.BindTexture(GL_TEXTURE_2D, 0);
    buf->host_dirty = false;
    return 0;
}

// Copy pixel data from a texture to a CPU buffer.
WEAK int get_pixels(void *user_context, buffer_t *buf, GLint format, GLint type, void *dest) {
    GLuint tex = get_texture_id(buf);
    global_state.BindFramebuffer(GL_FRAMEBUFFER, global_state.framebuffer_id);
    global_state.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, tex, 0);

    // Check that framebuffer is set up correctly
    GLenum status = global_state.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        global_state.BindFramebuffer(GL_FRAMEBUFFER, 0);
        error(user_context)
            << "Setting up GL framebuffer " << global_state.framebuffer_id << " failed " << status;
        return 1;
    }
    global_state.ReadPixels(0, 0, buf->extent[0], buf->extent[1], format, type, dest);
    global_state.BindFramebuffer(GL_FRAMEBUFFER, 0);
    return 0;
}

// Copy image data from texture back to host memory.
WEAK int halide_opengl_copy_to_host(void *user_context, buffer_t *buf) {
    CHECK_INITIALIZED(1);
    if (!buf->dev_dirty) {
        return 0;
    }

    if (!buf->host || !buf->dev) {
        debug_buffer(user_context, buf);
        error(user_context) << "Invalid copy_to_host operation: host or dev NULL";
        return 1;
    }

    #ifdef DEBUG_RUNTIME
    GLuint tex = get_texture_id(buf);
    debug(user_context) << "halide_copy_to_host: " << tex << "\n";
    #endif

    GLint internal_format, format, type;
    if (!get_texture_format(user_context, buf, &internal_format, &format, &type)) {
        error(user_context) << "Invalid texture format";
        return 1;
    }
    GLint width = buf->extent[0], height = buf->extent[1];

    // To download the texture directly, the colors must be stored interleaved
    // and rows must be stored consecutively.
    bool is_interleaved = (buf->stride[2] == 1 && buf->stride[0] == buf->extent[2]);
    bool is_packed = (buf->stride[1] == buf->extent[0] * buf->stride[0]);
    if (is_interleaved && is_packed) {
        global_state.PixelStorei(GL_PACK_ALIGNMENT, 1);
        uint8_t *host_ptr = buf->host + buf->elem_size *
            (buf->min[0] * buf->stride[0] +
             buf->min[1] * buf->stride[1] +
             buf->min[2] * buf->stride[2] +
             buf->min[3] * buf->stride[3]);
        get_pixels(user_context, buf, format, type, host_ptr);
    } else {
        debug(user_context)
            << "Warning: In copy_to_host, host buffer is not interleaved. Doing slow deinterleave.\n";

        size_t stride = width * buf->extent[2] * buf->elem_size;
        size_t size = height * stride;
        uint8_t *tmp = (uint8_t*)halide_malloc(user_context, size);
        if (!tmp) {
            error(user_context) << "halide_malloc failed inside copy_to_host";
            return -1;
        }

        global_state.PixelStorei(GL_PACK_ALIGNMENT, 1);
        if (int err = get_pixels(user_context, buf, format, type, tmp)) {
            halide_free(user_context, tmp);
            return err;
        }

        switch (type) {
        case GL_UNSIGNED_BYTE:
            interleaved_to_halide<uint8_t>(buf, (uint8_t*)tmp, width, height, buf->extent[2]);
            break;
        case GL_UNSIGNED_SHORT:
            interleaved_to_halide<uint16_t>(buf, (uint16_t*)tmp, width, height, buf->extent[2]);
            break;
        case GL_FLOAT:
            interleaved_to_halide<float>(buf, (float*)tmp, width, height, buf->extent[2]);
            break;
        }

        halide_free(user_context, tmp);
    }
    CHECK_GLERROR(1);
    buf->dev_dirty = false;
    return 0;
}

WEAK void set_int_param(void *user_context, const char *name,
                        GLint loc, GLint value) {
    global_state.Uniform1iv(loc, 1, &value);
}

WEAK void set_float_param(void *user_context, const char *name,
                          GLint loc, GLfloat value) {
    global_state.Uniform1fv(loc, 1, &value);
}

}}}} // namespace Halide::Runtime::Internal::OpenGL

using namespace Halide::Runtime::Internal::OpenGL;

//  Create wrappers that satisfy old naming conventions

extern "C" {

WEAK int halide_opengl_run(void *user_context,
                           void *state_ptr,
                           const char *entry_name,
                           int blocksX, int blocksY, int blocksZ,
                           int threadsX, int threadsY, int threadsZ,
                           int shared_mem_bytes,
                           size_t arg_sizes[], void *args[]) {
    CHECK_INITIALIZED(1);

    ModuleState *mod = (ModuleState *)state_ptr;
    if (!mod) {
        error(user_context) << "Internal error: module state is NULL";
        return -1;
    }

    KernelInfo *kernel = mod->kernel;
    if (!kernel) {
        error(user_context) << "Internal error: unknown kernel named '" << entry_name << "'";
        return 1;
    }

    global_state.UseProgram(kernel->program_id);
    CHECK_GLERROR(1);

    Argument *kernel_arg;
    bool bind_render_targets = true;

    // Copy input arguments to corresponding GLSL uniforms.
    GLint num_active_textures = 0;
    kernel_arg = kernel->arguments;
    for (int i = 0; args[i]; i++, kernel_arg = kernel_arg->next) {
        if (!kernel_arg) {
            error(user_context)
                << "Too many arguments passed to halide_opengl_run\n"
                << "Argument " << i << ": size=" << i << " value=" << args[i];
            return 1;
        }

        if (kernel_arg->kind == Argument::Outbuf) {
            // Check if the output buffer will be bound by the client instead of
            // the Halide runtime
            GLuint tex = *((GLuint *)args[i]);
            if (tex == (GLuint)HALIDE_OPENGL_CLIENT_BOUND) {
                bind_render_targets = false;
            }
            // Outbuf textures are handled explicitly below
            continue;
        } else if (kernel_arg->kind == Argument::Inbuf) {
            GLint loc =
                global_state.GetUniformLocation(kernel->program_id, kernel_arg->name);
            CHECK_GLERROR(1);
            if (loc == -1) {
                error(user_context) << "No sampler defined for input texture.";
                return 1;
            }
            GLuint tex = *((GLuint *)args[i]);
            global_state.ActiveTexture(GL_TEXTURE0 + num_active_textures);
            global_state.BindTexture(GL_TEXTURE_2D, tex);
            global_state.Uniform1iv(loc, 1, &num_active_textures);
            num_active_textures++;
            // TODO: check maximum number of active textures
        } else if (kernel_arg->kind == Argument::Var) {
            GLint loc =
                global_state.GetUniformLocation(kernel->program_id, kernel_arg->name);
            CHECK_GLERROR(1);
            if (loc == -1) {
                // Argument was probably optimized away by GLSL compiler.
                continue;
            }

            // Note: small integers are represented as floats in GLSL.
            switch (kernel_arg->type) {
            case Argument::Float:
                set_float_param(user_context, kernel_arg->name, loc, *(float*)args[i]);
                break;
            case Argument::Bool: {
                GLint value = *((bool*)args[i]) ? 1 : 0;
                set_int_param(user_context, kernel_arg->name, loc, value);
                break;
            }
            case Argument::Int8: {
                GLfloat value = *((int8_t*)args[i]);
                set_float_param(user_context, kernel_arg->name, loc, value);
                break;
            }
            case Argument::UInt8: {
                GLfloat value = *((uint8_t*)args[i]);
                set_float_param(user_context, kernel_arg->name, loc, value);
                break;
            }
            case Argument::Int16: {
                GLfloat value = *((int16_t*)args[i]);
                set_float_param(user_context, kernel_arg->name, loc, value);
                break;
            }
            case Argument::UInt16: {
                GLfloat value = *((uint16_t*)args[i]);
                set_float_param(user_context, kernel_arg->name, loc, value);
                break;
            }
            case Argument::Int32: {
                GLint value = *((int32_t*)args[i]);
                set_int_param(user_context, kernel_arg->name, loc, value);
                break;
            }
            case Argument::UInt32: {
                uint32_t value = *((uint32_t*)args[i]);
                GLint signed_value;
                if (value > 0x7fffffff) {
                    error(user_context)
                        << "OpenGL: argument '" << kernel_arg->name << "' is too large for GLint";
                    return -1;
                }
                signed_value = static_cast<GLint>(value);
                set_int_param(user_context, kernel_arg->name, loc, signed_value);
                break;
            }
            case Argument::Void:
                error(user_context) <<"OpenGL: Encountered invalid kernel argument type";
                return 1;
            }
        }
    }
    if (kernel_arg) {
        error(user_context) << "Too few arguments passed to halide_opengl_run";
        return 1;
    }

    // Prepare framebuffer for rendering to output textures.
    GLint output_min[2] = { 0, 0 };
    GLint output_extent[2] = { 0, 0 };

    if (bind_render_targets) {
        global_state.BindFramebuffer(GL_FRAMEBUFFER, global_state.framebuffer_id);
    }

    global_state.Disable(GL_CULL_FACE);
    global_state.Disable(GL_DEPTH_TEST);

    GLint num_output_textures = 0;
    kernel_arg = kernel->arguments;
    for (int i = 0; args[i]; i++, kernel_arg = kernel_arg->next) {
        if (kernel_arg->kind != Argument::Outbuf) continue;

        // TODO: GL_MAX_COLOR_ATTACHMENTS
        if (num_output_textures >= 1) {
            error(user_context)
                << "OpenGL ES 2.0 only supports one single output texture";
            return 1;
        }

        GLuint tex = *((GLuint*)args[i]);

        // Check to see if the object name is actually a FBO
        if (bind_render_targets) {
            debug(user_context)
                << "Output texture " << num_output_textures << ": " << tex << "\n";
            global_state.FramebufferTexture2D(GL_FRAMEBUFFER,
                                    GL_COLOR_ATTACHMENT0 + num_output_textures,
                                    GL_TEXTURE_2D, tex, 0);
            CHECK_GLERROR(1);
        }

        TextureInfo *texinfo = find_texture(tex);
        if (!texinfo) {
            error(user_context) << "Undefined output texture";
            return 1;
        }
        output_min[0] = texinfo->min[0];
        output_min[1] = texinfo->min[1];
        output_extent[0] = texinfo->extent[0];
        output_extent[1] = texinfo->extent[1];
        num_output_textures++;
    }
    // TODO: GL_MAX_DRAW_BUFFERS
    if (num_output_textures == 0) {
        error(user_context) << "kernel has no output";
        // TODO: cleanup
        return 1;
    } else {
        GLenum *draw_buffers = (GLenum*)
            malloc(num_output_textures * sizeof(GLenum));
        for (int i=0; i<num_output_textures; i++)
            draw_buffers[i] = GL_COLOR_ATTACHMENT0 + i;
        // TODO: disabled for now, since OpenGL ES 2 doesn't support multiple render
        // targets.
        //        global_state.DrawBuffers(num_output_textures, draw_buffers);
        free(draw_buffers);

        CHECK_GLERROR(1);
    }

    if (bind_render_targets) {
        // Check that framebuffer is set up correctly
        GLenum status = global_state.CheckFramebufferStatus(GL_FRAMEBUFFER);
        CHECK_GLERROR(1);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            error(user_context)
                << "Setting up GL framebuffer " << global_state.framebuffer_id
                << " failed (" << status << ")";
            // TODO: cleanup
            return 1;
        }
    }

    // Set vertex attributes
    GLint loc = global_state.GetUniformLocation(kernel->program_id, "output_extent");
    global_state.Uniform2iv(loc, 1, output_extent);
    CHECK_GLERROR(1);
    loc = global_state.GetUniformLocation(kernel->program_id, "output_min");
    global_state.Uniform2iv(loc, 1, output_min);
    CHECK_GLERROR(1);

    // Setup viewport
    global_state.Viewport(0, 0, output_extent[0], output_extent[1]);

    // Execute shader by drawing a screen-filling quad
    if (global_state.have_vertex_array_objects) {
        global_state.BindVertexArray(global_state.vertex_array_object);
    }
    GLint position = global_state.GetAttribLocation(kernel->program_id, "position");
    global_state.EnableVertexAttribArray(position);
    global_state.BindBuffer(GL_ARRAY_BUFFER, global_state.vertex_buffer);
    global_state.VertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    global_state.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, global_state.element_buffer);
    global_state.DrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_INT, NULL);
    global_state.DisableVertexAttribArray(position);
    global_state.BindBuffer(GL_ARRAY_BUFFER, 0);
    global_state.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    if (global_state.have_vertex_array_objects) {
        global_state.BindVertexArray(0);
    }
    CHECK_GLERROR(1);

    // Cleanup
    for (int i = 0; i < num_active_textures; i++) {
        global_state.ActiveTexture(GL_TEXTURE0 + i);
        global_state.BindTexture(GL_TEXTURE_2D, 0);
    }

    if (bind_render_targets) {
        global_state.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    return 0;
}

WEAK int halide_opengl_device_sync(void *user_context, struct buffer_t *) {
    CHECK_INITIALIZED(1);
    // TODO: glFinish()
    return 0;
}

// Called at the beginning of a code block generated by Halide. This function
// is responsible for setting up the OpenGL environment and compiling the GLSL
// code into a fragment shader.
WEAK int halide_opengl_init_kernels(void *user_context, void **state_ptr,
                                    const char *src, int size) {
    if (int error = halide_opengl_init(user_context)) {
        return error;
    }

    ModuleState **state = (ModuleState **)state_ptr;
    ModuleState *module = *state;
    if (!module) {
        module = (ModuleState *)malloc(sizeof(ModuleState));
        module->kernel = NULL;
        module->next = state_list;
        state_list = module;
        *state = module;
    }

    KernelInfo *kernel = module->kernel;
    if (!kernel) {
        kernel = create_kernel(user_context, src, size);
        if (!kernel) {
            error(user_context) << "Invalid kernel: " << src;
            return -1;
        }
        module->kernel = kernel;
    }

    if (kernel->program_id == 0) {
        // Compile shader
        kernel->shader_id = make_shader(user_context, GL_FRAGMENT_SHADER,
                                        kernel->source, NULL);

        // Link GLSL program
        GLuint program = global_state.CreateProgram();
        global_state.AttachShader(program, global_state.vertex_shader_id);
        global_state.AttachShader(program, kernel->shader_id);
        global_state.LinkProgram(program);
        GLint status;
        global_state.GetProgramiv(program, GL_LINK_STATUS, &status);
        if (!status) {
            GLint log_len;
            global_state.GetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
            char *log = (char*) malloc(log_len);
            global_state.GetProgramInfoLog(program, log_len, NULL, log);
            debug(user_context) << "Could not link GLSL program:\n"
                                << log << "\n";
            free(log);
            global_state.DeleteProgram(program);
            return -1;
        }
        kernel->program_id = program;
    }
    return 0;
}

WEAK const halide_device_interface *halide_opengl_device_interface() {
    return &opengl_device_interface;
}

WEAK void halide_opengl_context_lost(void *user_context) {
    if (!global_state.initialized) return;

    debug(user_context) << "halide_opengl_context_lost\n";
    for (ModuleState *mod = state_list; mod; mod = mod->next) {
        // Reset program handle to force recompilation.
        mod->kernel->program_id = 0;
    }

    TextureInfo *tex = global_state.textures;
    while (tex) {
        TextureInfo *next = tex->next;
        free(tex);
        tex = next;
    }

    global_state.init();
    return;
}

// This function is called to populate the buffer_t.dev field with a constant
// indicating that the OpenGL object corresponding to the buffer_t is bound by
// the app and not by the Halide runtime. For example, the buffer_t may be
// backed by an FBO already bound by the application.
WEAK uint64_t halide_opengl_output_client_bound() {
    return HALIDE_OPENGL_CLIENT_BOUND;
}

} // extern "C"

namespace Halide { namespace Runtime { namespace Internal { namespace OpenGL {

WEAK halide_device_interface opengl_device_interface = {
    halide_opengl_device_malloc,
    halide_opengl_device_free,
    halide_opengl_device_sync,
    halide_opengl_device_release,
    halide_opengl_copy_to_host,
    halide_opengl_copy_to_device,
  };

}}}} // namespace Halide::Runtime::Internal::OpenGL
