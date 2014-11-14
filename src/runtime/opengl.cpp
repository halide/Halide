#include "runtime_internal.h"
#include "../buffer_t.h"
#include "HalideRuntime.h"
#include "mini_opengl.h"

// This constant is used to indicate that the application will take
// responsibility for binding the output render target before calling the
// Halide function.
#define HALIDE_GLSL_CLIENT_BOUND ((uint64_t)-1)

// Implementation note: all function that directly or indirectly access the
// runtime state in halide_opengl_state must be declared as WEAK, otherwise
// the behavior at runtime is undefined.

// This function must be provided by the host environment to retrieve pointers
// to OpenGL API functions.
extern "C" void *halide_opengl_get_proc_address(void *user_context, const char *name);
extern "C" int halide_opengl_create_context(void *user_context);

extern "C" int isdigit(int c);

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
    GLFUNC(PFNGLUNIFORM2IVPROC, Uniform4iv);                            \
    GLFUNC(PFNGLUNIFORM1FVPROC, Uniform1fv);                            \
    GLFUNC(PFNGLUNIFORM1FVPROC, Uniform4fv);                            \
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

namespace Halide { namespace Runtime { namespace Internal {


enum OpenGLProfile {
    OpenGL,
    OpenGLES
};

struct Argument {
    // The kind of data stored in an argument
    enum Kind {
        Invalid,
        Uniform,                        // uniform variable
        Varying,                        // varying attribute
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

WEAK const char *kernel_marker = "/// KERNEL ";
WEAK const char *input_marker  = "/// IN_BUFFER ";
WEAK const char *output_marker = "/// OUT_BUFFER ";
WEAK const char *uniform_marker    = "/// UNIFORM ";
WEAK const char *varying_marker    = "/// VARYING ";
    
// ---------- Macros ----------

// Convenience macro for accessing state of the OpenGL runtime
#define ST global_state

// Ensure that OpenGL runtime is correctly initialized. Used in all public API
// functions.
#define CHECK_INITIALIZED(ERRORCODE)                            \
    if (!ST.initialized) {                                      \
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
            error(user_context) << __FILE__ << ":" << __LINE__ << "OpenGL error " << err; \
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
    return buf->dev & 0xffffffff;
}

WEAK void debug_buffer(void *user_context, buffer_t *buf) {
    debug(user_context)
        << "  dev: " << buf->dev << "\n"
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

    debug(user_context) << "SHADER SOURCE:\n"
			<< source << "\n";

    GLuint shader = ST.CreateShader(type);
    CHECK_GLERROR(1);
    ST.ShaderSource(shader, 1, (const GLchar **)&source, length);
    CHECK_GLERROR(1);
    ST.CompileShader(shader);
    CHECK_GLERROR(1);

    GLint shader_ok = 0;
    ST.GetShaderiv(shader, GL_COMPILE_STATUS, &shader_ok);
    if (!shader_ok) {
        print(user_context) << "Could not compile shader:\n";
        GLint log_len;
        ST.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        char *log = (char *)malloc(log_len);
        ST.GetShaderInfoLog(shader, log_len, NULL, log);
        print(user_context) << log << "\n";
        free(log);
        ST.DeleteShader(shader);
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
        } else if ((args = match_prefix(line, uniform_marker))) {
            if (Argument *arg =
                parse_argument(user_context, args, next_line - 1)) {
                arg->kind = Argument::Uniform;
                arg->next = kernel->arguments;
                kernel->arguments = arg;
            } else {
                halide_error(user_context, "Invalid VAR marker");
                goto error;
            }
        } else if ((args = match_prefix(line, varying_marker))) {
            if (Argument *arg =
                parse_argument(user_context, args, next_line - 1)) {
                arg->kind = Argument::Varying;
                arg->next = kernel->arguments;
                kernel->arguments = arg;
            } else {
                halide_error(user_context, "Invalid VARYING marker");
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
    ST.DeleteProgram(kernel->program_id);

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
    -1.0f, 1.0f,     1.0f, 1.0f
};
WEAK GLuint quad_indices[] = { 0, 1, 2, 3 };

WEAK void GlobalState::init() {
    initialized = false;
    profile = OpenGL;
    major_version = 2;
    minor_version = 0;
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
    if (ST.major_version >= 3) {
        GLint num_extensions = 0;
        ST.GetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
        for (int i = 0; i < num_extensions; i++) {
            const char *ext = (const char *)ST.GetStringi(GL_EXTENSIONS, i);
            if (strcmp(ext, name) == 0) {
                return true;
            }
        }
    } else {
        const char *start = (const char *)ST.GetString(GL_EXTENSIONS);
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
    if (ST.major_version >= 3) {
        ST.have_vertex_array_objects = true;
        load_gl_func(user_context, "glGenVertexArrays", (void**)&ST.GenVertexArrays);
        load_gl_func(user_context, "glBindVertexArray", (void**)&ST.BindVertexArray);
        load_gl_func(user_context, "glDeleteVertexArrays", (void**)&ST.DeleteVertexArrays);
    }

    ST.have_texture_rg =
        ST.major_version >= 3 ||
        (ST.profile == OpenGL &&
         extension_supported(user_context, "GL_ARB_texture_rg")) ||
        (ST.profile == OpenGLES &&
         extension_supported(user_context, "GL_EXT_texture_rg"));

    ST.have_texture_float =
        (ST.major_version >= 3) ||
        (ST.profile == OpenGL &&
         extension_supported(user_context, "GL_ARB_texture_float")) ||
        (ST.profile == OpenGLES &&
         extension_supported(user_context, "GL_OES_texture_float"));
}

WEAK const char *parse_int(const char *str, int *val) {
    if (!isdigit(*str)) return NULL;
    int v = 0;
    do {
        v = 10 * v + (*str++ - '0');
    } while (isdigit(*str));
    *val = v;
    return str;
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
    if (ST.initialized) {
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
    if (load_gl_func(user_context, "gl" #VAR, (void**)&ST.VAR) < 0) {   \
        return -1;                                                      \
    }
    USED_GL_FUNCTIONS;
#undef GLFUNC

    const char *version = (const char *)ST.GetString(GL_VERSION);
    const char *gles_version = match_prefix(version, "OpenGL ES ");
    int major, minor;
    if (gles_version && parse_opengl_version(gles_version, &major, &minor)) {
        ST.profile = OpenGLES;
        ST.major_version = major;
        ST.minor_version = minor;
    } else if (parse_opengl_version(version, &major, &minor)) {
        ST.profile = OpenGL;
        ST.major_version = major;
        ST.minor_version = minor;
    } else {
        ST.profile = OpenGL;
        ST.major_version = 2;
        ST.minor_version = 0;
    }
    init_extensions(user_context);
    debug(user_context)
        << "Halide running on OpenGL "
        << ((ST.profile == OpenGL) ? "" : "ES ")
        << major << "." << minor << "\n"
        << "  vertex_array_objects: "
        << (ST.have_vertex_array_objects ? "yes\n" : "no\n")
        << "  texture_rg: "
        << (ST.have_texture_rg ? "yes\n" : "no\n")
        << "  texture_float: "
        << (ST.have_texture_float ? "yes\n" : "no\n");

    // Initialize framebuffer.
    ST.GenFramebuffers(1, &ST.framebuffer_id);
    CHECK_GLERROR(1);

    // Initialize vertex and element buffers.
    GLuint buf[2];
    ST.GenBuffers(2, buf);
    ST.BindBuffer(GL_ARRAY_BUFFER, buf[0]);
    ST.BufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
    ST.BindBuffer(GL_ARRAY_BUFFER, 0);
    ST.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf[1]);
    ST.BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quad_indices), quad_indices, GL_STATIC_DRAW);
    ST.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    ST.vertex_buffer = buf[0];
    ST.element_buffer = buf[1];

    if (ST.have_vertex_array_objects) {
        ST.GenVertexArrays(1, &ST.vertex_array_object);
    }

    CHECK_GLERROR(1);
    ST.initialized = true;
    return 0;
}

// Release all data allocated by the runtime.
//
// The OpenGL context itself is generally managed by the host application, so
// we leave it untouched.
WEAK void halide_opengl_release(void *user_context) {
    if (!ST.initialized) return;

#ifdef DEBUG
    halide_printf(user_context, "halide_opengl_release\n");
#endif

    ST.DeleteFramebuffers(1, &ST.framebuffer_id);

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
    TextureInfo *tex = ST.textures;
    int freed_textures = 0;
    while (tex) {
        TextureInfo *next = tex->next;
        if (tex->halide_allocated) {
            ST.DeleteTextures(1, &tex->id);
            CHECK_GLERROR();
            freed_textures++;
        }
        free(tex);
        tex = next;
    }

    if (freed_textures > 0) {
        debug(user_context) << "halide_opengl_release: deleted "
                            << freed_textures << " dangling texture(s).\n";
    }

    ST.DeleteBuffers(1, &ST.vertex_buffer);
    ST.DeleteBuffers(1, &ST.element_buffer);
    if (ST.have_vertex_array_objects) {
        ST.DeleteVertexArrays(1, &ST.vertex_array_object);
    }

    ST = GlobalState();
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
        error(user_context) << "GLSL: Only uint8, uint16, and float textures are supported.";
        return false;
    }

    const int channels = buf->extent[2];
    if (channels <= 2 && !ST.have_texture_rg) {
        error(user_context) << "GLSL: This version of OpenGL doesn't support <=2 channels.";
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
        error(user_context) << "GLSL: Only 3 or 4 color channels are supported.";
        return false;
    }

    switch (ST.profile) {
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
    TextureInfo *texinfo = ST.textures;
    while (texinfo && texinfo->id != tex) {
        texinfo = texinfo->next;
    }
    return texinfo;
}

// Allocate a new texture matching the dimension and color format of the
// specified buffer.
WEAK int halide_opengl_dev_malloc(void *user_context, buffer_t *buf) {
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
        ST.BindTexture(GL_TEXTURE_2D, tex);
        ST.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        ST.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
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
        ST.GenTextures(1, &tex);
        CHECK_GLERROR(1);

        // Set parameters for this texture: no interpolation and clamp to edges.
        ST.BindTexture(GL_TEXTURE_2D, tex);
        ST.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        ST.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        ST.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        ST.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
        ST.TexImage2D(GL_TEXTURE_2D, 0, internal_format,
                      width, height, 0, format, type, NULL);
        CHECK_GLERROR(1);

        buf->dev = tex;
        halide_allocated = true;
        debug(user_context) << "Allocated texture " << tex
                            << " of size " << width << " x " << height << "\n";

        ST.BindTexture(GL_TEXTURE_2D, 0);
    }

    // Record main information about texture and remember it for later. In
    // halide_opengl_dev_run we are only given the texture ID and not the full
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

        texinfo->next = ST.textures;
        ST.textures = texinfo;
    }
    return 0;
}

// Delete all texture information associated with a buffer. The OpenGL texture
// itself is only deleted if it was actually allocated by Halide and not
// provided by the host application.
WEAK int halide_opengl_dev_free(void *user_context, buffer_t *buf) {
    CHECK_INITIALIZED(1);

    GLuint tex = get_texture_id(buf);
    if (tex == 0) {
        return 0;
    }

    // Look up corresponding TextureInfo and unlink it from the list.
    TextureInfo **ptr = &ST.textures;
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
        ST.DeleteTextures(1, &tex);
        CHECK_GLERROR(1);
        buf->dev = 0;
    }

    free(texinfo);
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

        // Create the vertex shader the runtime will output boilerplate for the
        // vertex shader based on a fixed program plus arguments obtained from
        // the comment header passed in the fragment shader. Since there is a
        // relatively small number of vertices (i.e. usually only four) per
        // vertex expressions interpolated by varying attributes are evaluated
        // by host code on the CPU and passed to the GPU as values in the
        // vertex buffer.
        enum { PrinterLength = 1024*256 };
        Printer<StringStreamPrinter,PrinterLength> vertex_src(user_context);

        // Count the number of varying attributes, this is 2 for the spatial
        // x and y coordinates, plus the number of scalar varying attribute
        // expressions pulled out of the fragment shader.
        int num_varying_float = 2;

        for (Argument* arg = kernel->arguments; arg; arg=arg->next) {
            if (arg->kind == Argument::Varying)
                ++num_varying_float;
        }

        int num_packed_varying_float = ((num_varying_float + 3) & ~0x3) / 4;

        for (int i = 0; i != num_packed_varying_float; ++i) {
            vertex_src << "attribute vec4 _varyingf" << i << "_attrib;\n";
            vertex_src << "varying   vec4 _varyingf" << i << ";\n";
        }
        
        vertex_src << "uniform ivec2 output_min;\n"
                   << "uniform ivec2 output_extent;\n"
                   << "void main() {\n"

                   // Host codegen always passes the spatial vertex coordinates
                   // in the first two elements of the _varyingf0_attrib
                   << "    vec2 position = vec2(_varyingf0_attrib[0], _varyingf0_attrib[1]);\n"
                   << "    gl_Position = vec4(position, 0.0, 1.0);\n"
                   << "    vec2 texcoord = 0.5 * position + 0.5;\n"
                   << "    vec2 pixcoord = texcoord * vec2(output_extent.xy) + vec2(output_min.xy);\n";

        // Copy through all of the varying attributes
        for (int i = 0; i != num_packed_varying_float; ++i) {
            vertex_src << "    _varyingf" << i << " = _varyingf" << i << "_attrib;\n";
        }

        vertex_src << "    _varyingf0.xy = pixcoord;\n";

        vertex_src << "}\n";

        // Check to see if there was sufficient storage for the vertex program.
        if (vertex_src.size() >= PrinterLength) {
            error(user_context) << "Vertex shader source truncated";
            return 1;
        }
        
        // Initialize vertex shader.
        GLuint vertex_shader_id = make_shader(user_context,
                                              GL_VERTEX_SHADER, vertex_src.buf, NULL);
        if (vertex_shader_id == 0) {
            halide_error(user_context, "Failed to create vertex shader");
            return 1;
        }
        
        // Create the fragment shader
        GLuint fragment_shader_id = make_shader(user_context, GL_FRAGMENT_SHADER,
                                                kernel->source, NULL);
        // Link GLSL program
        GLuint program = ST.CreateProgram();
        ST.AttachShader(program, vertex_shader_id);
        ST.AttachShader(program, fragment_shader_id);
        ST.LinkProgram(program);

        // Release the individual shaders
        ST.DeleteShader(vertex_shader_id);
        ST.DeleteShader(fragment_shader_id);
        
        GLint status;
        ST.GetProgramiv(program, GL_LINK_STATUS, &status);
        if (!status) {
            GLint log_len;
            ST.GetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
            char *log = (char*) malloc(log_len);
            ST.GetProgramInfoLog(program, log_len, NULL, log);
            debug(user_context) << "Could not link GLSL program:\n"
                                << log << "\n";
            free(log);
            ST.DeleteProgram(program);
            return -1;
        }
        kernel->program_id = program;
    }
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
WEAK int halide_opengl_copy_to_dev(void *user_context, buffer_t *buf) {
    int err = halide_opengl_dev_malloc(user_context, buf);
    if (err) {
        return err;
    }

    CHECK_INITIALIZED(1);
    if (!buf->host_dirty) {
        return 0;
    }

    if (!buf->host || !buf->dev) {
        debug_buffer(user_context, buf);
        error(user_context) << "Invalid copy_to_dev operation: host or dev NULL";
        return 1;
    }

    GLuint tex = get_texture_id(buf);
    debug(user_context) << "halide_copy_to_dev: " << tex << "\n";

    ST.BindTexture(GL_TEXTURE_2D, tex);
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
        ST.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        uint8_t *host_ptr = buf->host + buf->elem_size *
            (buf->min[0] * buf->stride[0] +
             buf->min[1] * buf->stride[1] +
             buf->min[2] * buf->stride[2] +
             buf->min[3] * buf->stride[3]);
        ST.TexSubImage2D(GL_TEXTURE_2D, 0,
                         0, 0, width, height,
                         format, type, host_ptr);
        CHECK_GLERROR(1);
    } else {
        debug(user_context)
            << "Warning: In copy_to_dev, host buffer is not interleaved. Doing slow interleave.\n";

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

        ST.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        ST.TexSubImage2D(GL_TEXTURE_2D, 0,
                         0, 0, width, height,
                         format, type, tmp);
        CHECK_GLERROR(1);

        halide_free(user_context, tmp);
    }
    ST.BindTexture(GL_TEXTURE_2D, 0);
    buf->host_dirty = false;
    return 0;
}

// Copy pixel data from a texture to a CPU buffer.
WEAK int get_pixels(void *user_context, buffer_t *buf, GLint format, GLint type, void *dest) {
    GLuint tex = get_texture_id(buf);
    ST.BindFramebuffer(GL_FRAMEBUFFER, ST.framebuffer_id);
    ST.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, tex, 0);

    // Check that framebuffer is set up correctly
    GLenum status = ST.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        ST.BindFramebuffer(GL_FRAMEBUFFER, 0);
        error(user_context)
            << "Setting up GL framebuffer " << ST.framebuffer_id << " failed " << status;
        return 1;
    }
    ST.ReadPixels(0, 0, buf->extent[0], buf->extent[1], format, type, dest);

    ST.BindFramebuffer(GL_FRAMEBUFFER, 0);
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
        ST.PixelStorei(GL_PACK_ALIGNMENT, 1);
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

        ST.PixelStorei(GL_PACK_ALIGNMENT, 1);

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
    ST.Uniform1iv(loc, 1, &value);
}

WEAK void set_float_param(void *user_context, const char *name,
                          GLint loc, GLfloat value) {
    ST.Uniform1fv(loc, 1, &value);
}
    
class IndexSorter {
public:
    IndexSorter(float* values_) : values(values_) {  }
    
    bool operator()(int a, int b) { return values[a] < values[b]; }
    float* values;
};

extern "C"
WEAK int halide_opengl_dev_run(
    void *user_context,
    void *state_ptr,
    const char *entry_name,
    int blocksX, int blocksY, int blocksZ,
    int threadsX, int threadsY, int threadsZ,
    int shared_mem_bytes,
    size_t arg_sizes[],
    void *args[],
    char** attribute_names,
    int num_padded_attributes,
    float* vertex_buffer,
    int num_coords_dim0,
    int num_coords_dim1) {
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

    ST.UseProgram(kernel->program_id);
    CHECK_GLERROR(1);

    // TODO(abstephensg) it would be great to codegen these vec4 uniform buffers
    // directly, instead of passing an array of arguments and then copying them
    // out at runtime.

    // Determine the number of float and int uniform parameters. This code
    // follows the argument packing convention in CodeGen_GPU_Host and
    // CodeGen_OpenGL_Dev
    int num_uniform_floats = 0;
    int num_uniform_ints = 0;

    Argument *kernel_arg = kernel->arguments;
    for (int i = 0; args[i]; i++, kernel_arg = kernel_arg->next) {

        // Check for a mismatch between the number of arguments declared in the
        // fragment shader source header and the number passed to this function
        if (!kernel_arg) {
            error(user_context)
            << "Too many arguments passed to halide_opengl_dev_run\n"
            << "Argument " << i << ": size=" << i << " value=" << args[i];
            return 1;
        }

        // Count the number of float and int uniform parameters.
        if (kernel_arg->kind == Argument::Uniform) {
            switch (kernel_arg->type) {
                case Argument::Float:
                // Integer parameters less than 32 bits wide are passed as
                // normalized float values
                case Argument::Int8:
                case Argument::UInt8:
                case Argument::Int16:
                case Argument::UInt16:
                    ++num_uniform_floats;
                    break;
                case Argument::Bool:
                case Argument::Int32:
                case Argument::UInt32:
                    ++num_uniform_ints;
                    break;
                default:
                    error(user_context) << "GLSL: Encountered invalid kernel argument type";
                    return 1;
            }
        }
    }

    // Pad up to a multiple of four
    int num_padded_uniform_floats = (num_uniform_floats + 0x3) & ~0x3;
    int num_padded_uniform_ints   = (num_uniform_ints + 0x3) & ~0x3;

    // Allocate storage for the packed arguments
    float uniform_float[num_padded_uniform_floats];
    int   uniform_int[num_padded_uniform_ints];

    bool bind_render_targets = true;

    // Copy input arguments to corresponding GLSL uniforms.
    GLint num_active_textures = 0;
    int uniform_float_idx = 0;
    int uniform_int_idx = 0;

    kernel_arg = kernel->arguments;
    for (int i = 0; args[i]; i++, kernel_arg = kernel_arg->next) {

        if (kernel_arg->kind == Argument::Outbuf) {

            // Check if the output buffer will be bound by the client instead of
            // the Halide runtime
            GLuint tex = *((GLuint *)args[i]);
            if (tex == (GLuint)HALIDE_GLSL_CLIENT_BOUND) {
                bind_render_targets = false;
            }
            // Outbuf textures are handled explicitly below
            continue;
        } else if (kernel_arg->kind == Argument::Inbuf) {
            GLint loc =
                ST.GetUniformLocation(kernel->program_id, kernel_arg->name);
            CHECK_GLERROR(1);
            if (loc == -1) {
                error(user_context) << "No sampler defined for input texture.";
                return 1;
            }
            GLuint tex = *((GLuint *)args[i]);
            ST.ActiveTexture(GL_TEXTURE0 + num_active_textures);
            ST.BindTexture(GL_TEXTURE_2D, tex);

            // Textures not created by the Halide runtime might not have
            // parameters set, or might have had parameters set differently 
            ST.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            ST.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            ST.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            ST.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            ST.Uniform1iv(loc, 1, &num_active_textures);
            num_active_textures++;
            // TODO: check maximum number of active textures
        } else if (kernel_arg->kind == Argument::Uniform) {

            // Copy the uniform parameter into the packed scalar list
            // corresponding to its type.

            // Note: small integers are represented as floats in GLSL.
            switch (kernel_arg->type) {
            case Argument::Float:
                uniform_float[uniform_float_idx++] = *(float*)args[i];
                break;
            case Argument::Bool:
                uniform_int[uniform_int_idx++] = *((bool*)args[i]) ? 1 : 0;
                break;
            case Argument::Int8:
                uniform_float[uniform_float_idx++] = *((int8_t*)args[i]);
                break;
            case Argument::UInt8:
                uniform_float[uniform_float_idx++] = *((uint8_t*)args[i]);
                break;
            case Argument::Int16: {
                uniform_float[uniform_float_idx++] = *((int16_t*)args[i]);
                break;
            }
            case Argument::UInt16: {
                uniform_float[uniform_float_idx++] = *((uint16_t*)args[i]);
                break;
            }
            case Argument::Int32: {
                uniform_int[uniform_int_idx++] = *((int32_t*)args[i]);
                break;
            }
            case Argument::UInt32: {
                uint32_t value = *((uint32_t*)args[i]);
                if (value > 0x7fffffff) {
                    error(user_context)
                        << "GLSL: argument '" << kernel_arg->name << "' is too large for GLint";
                    return -1;
                }
                uniform_int[uniform_int_idx++] = static_cast<GLint>(value);
                break;
            }
            case Argument::Void:
                error(user_context) <<"GLSL: Encountered invalid kernel argument type";
                return 1;
            }
        }
    }

    if (kernel_arg) {
        halide_error(user_context, "Too few arguments passed to halide_opengl_dev_run");
        return 1;
    }

    // Set the packed uniform int parameters
    for (int idx = 0; idx != num_padded_uniform_ints; idx += 4) {

        // Produce the uniform parameter name without using the std library.
        Printer<StringStreamPrinter,16> name(user_context);
        name << "_uniformi" << (idx/4);

        GLint loc = ST.GetUniformLocation(kernel->program_id, name.str());
        CHECK_GLERROR(1);
        if (loc == -1) {
            // Argument was probably optimized away by GLSL compiler.
            continue;
        }

        ST.Uniform4iv(loc,1,&uniform_int[idx]);
    }

    // Set the packed uniform float parameters
    for (int idx = 0; idx != num_padded_uniform_floats; idx += 4) {

        // Produce the uniform parameter name without using the std library.
        Printer<StringStreamPrinter,16> name(user_context);
        name << "_uniformf" << (idx/4);

        GLint loc = ST.GetUniformLocation(kernel->program_id, name.str());
        CHECK_GLERROR(1);
        if (loc == -1) {
            // Argument was probably optimized away by GLSL compiler.
            continue;
        }

        ST.Uniform4fv(loc,1,&uniform_float[idx]);
    }

    // Prepare framebuffer for rendering to output textures.
    GLint output_min[2] = { 0, 0 };
    GLint output_extent[2] = { 0, 0 };

    if (bind_render_targets) {
        ST.BindFramebuffer(GL_FRAMEBUFFER, ST.framebuffer_id);
    }

    ST.Disable(GL_CULL_FACE);
    ST.Disable(GL_DEPTH_TEST);

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
            ST.FramebufferTexture2D(GL_FRAMEBUFFER,
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
        //        ST.DrawBuffers(num_output_textures, draw_buffers);
        free(draw_buffers);

        CHECK_GLERROR(1);
    }

    if (bind_render_targets) {
        // Check that framebuffer is set up correctly
        GLenum status = ST.CheckFramebufferStatus(GL_FRAMEBUFFER);
        CHECK_GLERROR(1);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            error(user_context)
                << "Setting up GL framebuffer " << ST.framebuffer_id
                << " failed (" << status << ")";
            // TODO: cleanup
            return 1;
        }
    }

    // Set vertex attributes
    GLint loc = ST.GetUniformLocation(kernel->program_id, "output_extent");
    ST.Uniform2iv(loc, 1, output_extent);
    CHECK_GLERROR(1);
    
    loc = ST.GetUniformLocation(kernel->program_id, "output_min");
    ST.Uniform2iv(loc, 1, output_min);
    CHECK_GLERROR(1);

#if 0 // DEBUG_RUNTIME
    debug(user_context) << "output_extent: " << output_extent[0] << "," << output_extent[1] << "\n";
    debug(user_context) << "output_min: " << output_min[0] << "," << output_min[1] << "\n";
#endif


    // TODO(abestephensg): Sort coordinate dimensions when the linear solver is integrated
    // Sort the coordinates
    /*
    int sorted_order0[num_coords_dim0];
    for (int i=0;i!=num_coords_dim0;++i)
        sorted_order0[i] = i;

    int sorted_order1[num_coords_dim1];
    for (int i=0;i!=num_coords_dim1;++i)
        sorted_order1[i] = i;

    std::sort(&sorted_order0[0], &sorted_order0[num_coords_dim0], IndexSorter(coords_per_dim[0]));
    std::sort(&sorted_order1[0], &sorted_order1[num_coords_dim1], IndexSorter(coords_per_dim[1]));
    */

#if 0 // DEBUG_RUNTIME
    debug(user_context) << "Sorted x coords: ";
    for (int i=0;i!=num_coords_dim0;++i)
        debug(user_context) << coords_per_dim[0][sorted_order0[i]] << " ";
    debug(user_context) << "\n";
    
    debug(user_context) << "Sorted y coords: ";
    for (int i=0;i!=num_coords_dim1;++i)
        debug(user_context) << coords_per_dim[1][sorted_order1[i]] << " ";
    debug(user_context) << "\n";
#endif
    
    // Construct an element buffer using the sorted vertex order
    int width = num_coords_dim0;
    int height = num_coords_dim1;

    int vertex_buffer_size = width*height*num_padded_attributes;

    int element_buffer_size = (width-1)*(height-1)*6;
    int element_buffer[element_buffer_size];
    
    int idx = 0;
    for (int h=0;h!=(height-1);++h) {
        for (int w=0;w!=(width-1);++w) {

            // TODO(abestephensg): Use sorted coordinates when integrated
            int v = w+h*width;
            element_buffer[idx++] = v;
            element_buffer[idx++] = v+1;
            element_buffer[idx++] = v+width+1;
            
            element_buffer[idx++] = v+width+1;
            element_buffer[idx++] = v+width;
            element_buffer[idx++] = v;
        }
    }
    

#if 0 // DEBUG_RUNTIME
    debug(user_context) << "Vertex buffer:";
    for (int i=0;i!=vertex_buffer_size;++i) {
        if (!(i%num_padded_attributes)) {
	  debug(user_context) << "\n";
        }
        debug(user_context) << vertex_buffer[i] << " ";
    }
    debug(user_context) << "\n";
    debug(user_context) << "\n";
    
    debug(user_context) << "Element buffer:";
    for (int i=0;i!=element_buffer_size;++i) {
        if (!(i%3)) {
            debug(user_context) << "\n";
        }        
        debug(user_context) << element_buffer[i] << " ";
    }
    debug(user_context) << "\n";
#endif 
    
    // Setup viewport
    ST.Viewport(0, 0, output_extent[0], output_extent[1]);

    // Setup the vertex and element buffers
    GLuint vertex_array_object = 0;
    if (ST.have_vertex_array_objects) {
        ST.GenVertexArrays(1,&vertex_array_object);
        ST.BindVertexArray(vertex_array_object);
    }

    GLuint vertex_buffer_id;
    ST.GenBuffers(1,&vertex_buffer_id);
    ST.BindBuffer(GL_ARRAY_BUFFER, vertex_buffer_id);
    ST.BufferData(GL_ARRAY_BUFFER, sizeof(float)*vertex_buffer_size, vertex_buffer, GL_STATIC_DRAW);
    CHECK_GLERROR(1);
    
    GLuint element_buffer_id;
    ST.GenBuffers(1,&element_buffer_id);
    ST.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer_id);
    ST.BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(float)*element_buffer_size, element_buffer, GL_STATIC_DRAW);
    CHECK_GLERROR(1);

    // The num_padded_attributes argument is the number of vertex attributes,
    // including the spatial x and y coordinates, padded up to a multiple of
    // four so that the attributes may be packed into vec4 slots.
    int num_packed_attributes = num_padded_attributes/4;

    // Set up the per vertex attributes
    GLint attrib_ids[num_packed_attributes];

    for (int i=0;i!=num_packed_attributes;i++) {

        // TODO(abstephensg): Switch to glBindAttribLocation
        GLint attrib_id = ST.GetAttribLocation(kernel->program_id, attribute_names[i]);
        attrib_ids[i] = attrib_id;

        // Check to see if the varying attribute was simplified out of the
        // program by the GLSL compiler. This may occur because the .varying attribute
        // let expression might be unused, but it is never run through a simplification
        // pass--with Let simplification turned on--since the pass would remove the
        // .varying variable in several cases.
        if (attrib_id == -1) {
          continue;
        }

        ST.VertexAttribPointer(attrib_id, 4, GL_FLOAT, GL_FALSE /* Normalized */, sizeof(GLfloat)*num_padded_attributes, (void*)(i*sizeof(GLfloat)*4));
        CHECK_GLERROR(1);
        
        ST.EnableVertexAttribArray(attrib_id);
        CHECK_GLERROR(1);
    }


    // Draw the scene
    ST.DrawElements(GL_TRIANGLES, element_buffer_size, GL_UNSIGNED_INT, NULL);
    CHECK_GLERROR(1);
    
    for (int i=0;i!=num_packed_attributes;++i) {
        if (attrib_ids[i] != -1)
            ST.DisableVertexAttribArray(attrib_ids[i]);
    }

    // Cleanup
    for (int i = 0; i < num_active_textures; i++) {
        ST.ActiveTexture(GL_TEXTURE0 + i);
        ST.BindTexture(GL_TEXTURE_2D, 0);
    }
    
    if (bind_render_targets) {
        ST.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    ST.BindBuffer(GL_ARRAY_BUFFER, 0);
    ST.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    if (ST.have_vertex_array_objects) {
        ST.BindVertexArray(0);
        ST.DeleteVertexArrays(1,&vertex_array_object);
    }
    
    ST.DeleteBuffers(1,&vertex_buffer_id);
    ST.DeleteBuffers(1,&element_buffer_id);

    return 0;
}

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK void halide_opengl_context_lost(void *user_context) {
    if (!ST.initialized) return;

    debug(user_context) << "halide_opengl_context_lost\n";
    for (ModuleState *mod = state_list; mod; mod = mod->next) {
        // Reset program handle to force recompilation.
        mod->kernel->program_id = 0;
    }

    TextureInfo *tex = ST.textures;
    while (tex) {
        TextureInfo *next = tex->next;
        free(tex);
        tex = next;
    }

    global_state.init();
    return;
}

WEAK uint64_t halide_opengl_output_client_bound() {
    return HALIDE_GLSL_CLIENT_BOUND;
}

//  Create wrappers that satisfy old naming conventions

WEAK void halide_release(void *user_context) {
    halide_opengl_release(user_context);
}

WEAK int halide_dev_malloc(void *user_context, buffer_t *buf) {
    return halide_opengl_dev_malloc(user_context, buf);
}

WEAK int halide_dev_free(void *user_context, buffer_t *buf) {
    return halide_opengl_dev_free(user_context, buf);
}

WEAK int halide_copy_to_host(void *user_context, buffer_t *buf) {
    return halide_opengl_copy_to_host(user_context, buf);
}

WEAK int halide_copy_to_dev(void *user_context, buffer_t *buf) {
    return halide_opengl_copy_to_dev(user_context, buf);
}

WEAK int halide_dev_run(void *user_context,
                          void *state_ptr,
                          const char *entry_name,
                          int blocksX, int blocksY, int blocksZ,
                          int threadsX, int threadsY, int threadsZ,
                          int shared_mem_bytes,
                          size_t arg_sizes[], void *args[],
                          char** attribute_names,
                          int num_padded_attributes,
                          float* vertex_buffer,
                          int num_coords_dim0,
                          int num_coords_dim1) {
    return halide_opengl_dev_run(user_context, state_ptr,
                                 entry_name,
                                 blocksX, blocksY, blocksZ,
                                 threadsX, threadsY, threadsY,
                                 shared_mem_bytes,
                                 arg_sizes, args,
                                 attribute_names,
                                 num_padded_attributes,
                                 vertex_buffer,
                                 num_coords_dim0,
                                 num_coords_dim1);
}

WEAK int halide_dev_sync(void *user_context) {
    return halide_opengl_dev_sync(user_context);
}

WEAK int halide_init_kernels(void *user_context, void **state_ptr,
                             const char *src, int size) {
    return halide_opengl_init_kernels(user_context, state_ptr, src, size);
}
}
