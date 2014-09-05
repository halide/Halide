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
    GLFUNC(PFNGLREADPIXELS, ReadPixels)

// ---------- Types ----------

namespace Halide { namespace Runtime { namespace Internal {

enum ArgumentKind {
    ARGKIND_NONE,
    ARGKIND_VAR,
    ARGKIND_INBUF,
    ARGKIND_OUTBUF
};

enum ArgumentType {
    ARGTYPE_NONE,
    ARGTYPE_BOOL,
    ARGTYPE_FLOAT,
    ARGTYPE_INT8,
    ARGTYPE_INT16,
    ARGTYPE_INT32,
    ARGTYPE_UINT8,
    ARGTYPE_UINT16,
    ARGTYPE_UINT32
};

struct Argument {
    char *name;
    ArgumentKind kind;
    ArgumentType type;
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
    GLenum format;                      // internal format: GL_RGBA, ...
    bool halide_allocated;              // allocated by us or host app?
    TextureInfo *next;
};

struct ModuleState {
    KernelInfo *kernel;
    ModuleState *next;
};

// All persistent state maintained by the runtime.
struct GlobalState {
    bool initialized;

    // Various objects shared by all filter kernels
    GLuint vertex_shader_id;
    GLuint framebuffer_id;
    GLuint vertex_buffer;
    GLuint element_buffer;

    // A list of all textures that are still active
    TextureInfo *textures;

    ModuleState *state_list;

    // Declare pointers used OpenGL functions
#define GLFUNC(PTYPE,VAR) PTYPE VAR
    USED_GL_FUNCTIONS;
#undef GLFUNC
};

// ---------- Static variables ----------

WEAK GlobalState global_state;

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

// ---------- Macros ----------

// Convenience macro for accessing state of the OpenGL runtime
#define ST global_state

// Ensure that OpenGL runtime is correctly initialized. Used in all public API
// functions.
#define CHECK_INITIALIZED(ERRORCODE)                            \
    if (!ST.initialized) {                                      \
        halide_error(user_context, "OpenGL runtime not initialized.\n"); \
        return ERRORCODE;                                       \
    }

// Macro for error checking.
#ifdef DEBUG
#define LOG_GLERROR(ERR)                                        \
    halide_printf(user_context,                                 \
                  "%s:%d: OpenGL error 0x%04x\n",               \
                  __FILE__, __LINE__, (ERR))
#else
#define LOG_GLERROR(ERR)
#endif

#define CHECK_GLERROR(ERRORCODE) do {                                   \
        GLenum err = global_state.GetError();                           \
        if (err != GL_NO_ERROR) {                                       \
            LOG_GLERROR(err);                                           \
            halide_error(user_context, "OpenGL error");                 \
            return ERRORCODE;                                           \
        }} while (0)


// ---------- Helper functions ----------

extern "C" void *malloc(size_t);
extern "C" void free(void*);

WEAK char *strndup(const char *s, size_t n) {
    char *p = (char*)malloc(n+1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

WEAK GLuint get_texture_id(buffer_t *buf) {
    return buf->dev & 0xffffffff;
}

WEAK void print_buffer(void *user_context, buffer_t *buf) {
    halide_printf(user_context, "  dev: %ul\n", buf->dev);
    halide_printf(user_context, "  host: %p\n", buf->host);
    halide_printf(user_context, "  extent: %d %d %d %d\n",
                  buf->extent[0], buf->extent[1], buf->extent[2], buf->extent[3]);
    halide_printf(user_context, "  stride: %d %d %d %d\n",
                  buf->stride[0], buf->stride[1], buf->stride[2], buf->stride[3]);
    halide_printf(user_context, "  min: %d %d %d %d\n",
                  buf->min[0], buf->min[1], buf->min[2], buf->min[3]);
    halide_printf(user_context, "  elem_size: %d\n", buf->elem_size);
    halide_printf(user_context, "  host_dirty: %d, dev_dirty: %d\n",
                  buf->host_dirty, buf->dev_dirty);
}

WEAK GLuint make_shader(void *user_context, GLenum type,
                        const char *source, GLint *length) {
    GLuint shader = ST.CreateShader(type);
    CHECK_GLERROR(1);
    ST.ShaderSource(shader, 1, (const GLchar **)&source, length);
    CHECK_GLERROR(1);
    ST.CompileShader(shader);
    CHECK_GLERROR(1);

    GLint shader_ok = 0;
    ST.GetShaderiv(shader, GL_COMPILE_STATUS, &shader_ok);
    if (!shader_ok) {
        halide_printf(user_context, "Could not compile shader:\n");
        GLint log_len;
        ST.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        char *log = (char *)malloc(log_len);
        ST.GetShaderInfoLog(shader, log_len, NULL, log);
        halide_printf(user_context, "%s", log);
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
    ArgumentType type = ARGTYPE_NONE;
    if ((name = match_prefix(src, "float "))) {
        type = ARGTYPE_FLOAT;
    } else if ((name = match_prefix(src, "bool "))) {
        type = ARGTYPE_BOOL;
    } else if ((name = match_prefix(src, "int8_t "))) {
        type = ARGTYPE_INT8;
    } else if ((name = match_prefix(src, "int16_t "))) {
        type = ARGTYPE_INT16;
    } else if ((name = match_prefix(src, "int32_t "))) {
        type = ARGTYPE_INT32;
    } else if ((name = match_prefix(src, "uint8_t "))) {
        type = ARGTYPE_UINT8;
    } else if ((name = match_prefix(src, "uint16_t "))) {
        type = ARGTYPE_UINT16;
    } else if ((name = match_prefix(src, "uint32_t "))) {
        type = ARGTYPE_UINT32;
    }
    if (type == ARGTYPE_NONE) {
        halide_error(user_context, "Internal error: argument type not supported");
        return NULL;
    }

    Argument *arg = (Argument *)malloc(sizeof(Argument));
    arg->name = strndup(name, end - name);
    arg->type = type;
    arg->kind = ARGKIND_NONE;
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

    #ifdef DEBUG
    {
        // Android logcat output clips at ~1000 character chunks by default;
        // to avoid clipping the interesting stuff, emit a line at a time.
        // This is less efficient, but it's DEBUG-only.
        halide_printf(user_context, "Compiling GLSL kernel (size=%d):\n", size);
        const int kBufSize = 255;
        char buf[kBufSize + 1];
        const char* s = src;
        int d = 0;
        while (s < src + size) {
            while (*s != '\n' && *s != '\0' && d < kBufSize) {
                buf[d++] = *s++;
            }
            buf[d++] = '\0';
            halide_printf(user_context, "%s\n", buf);
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
                arg->kind = ARGKIND_VAR;
                arg->next = kernel->arguments;
                kernel->arguments = arg;
            } else {
                halide_error(user_context, "Invalid VAR marker");
                goto error;
            }
        } else if ((args = match_prefix(line, input_marker))) {
            if (Argument *arg = parse_argument(user_context, args, next_line - 1)) {
                arg->kind = ARGKIND_INBUF;
                arg->next = kernel->arguments;
                kernel->arguments = arg;
            } else {
                halide_error(user_context, "Invalid IN_BUFFER marker");
                goto error;
            }
        } else if ((args = match_prefix(line, output_marker))) {
            if (Argument *arg = parse_argument(user_context, args, next_line - 1)) {
                arg->kind = ARGKIND_OUTBUF;
                arg->next = kernel->arguments;
                kernel->arguments = arg;
            } else {
                halide_error(user_context, "Invalid OUT_BUFFER marker");
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
    ST.DeleteShader(kernel->shader_id);

    Argument *arg = kernel->arguments;
    while (arg) {
        Argument *next = arg->next;
        free(arg);
        arg = next;
    }
    free(kernel);
}

// Initialize the runtime, in particular all fields in halide_opengl_state.
WEAK int halide_opengl_init(void *user_context) {
    if (ST.initialized) return 0;

    // Make a context if there isn't one
    if (halide_opengl_create_context(user_context)) {
        halide_printf(user_context, "Failed to make opengl context\n");
        return 1;
    }

    // Initialize pointers to OpenGL functions.
#define GLFUNC(TYPE, VAR)                                               \
    ST.VAR = (TYPE)halide_opengl_get_proc_address(user_context, "gl" #VAR); \
    if (!ST.VAR) {                                                      \
        halide_printf(user_context, "Could not load function pointer for %s\n", "gl" #VAR); \
        return 1;                                                         \
    }
    USED_GL_FUNCTIONS;
#undef GLFUNC

    ST.textures = NULL;
    ST.state_list = NULL;

    // Initialize framebuffer.
    ST.GenFramebuffers(1, &ST.framebuffer_id);
    CHECK_GLERROR(1);

    // Initialize vertex shader.
    ST.vertex_shader_id = make_shader(user_context, GL_VERTEX_SHADER,
                                      vertex_shader_src, NULL);
    if (ST.vertex_shader_id == 0) {
        halide_error(user_context, "Failed to create vertex shader");
        return 1;
    }

    // Vertices and their order in a triangle strip for rendering a square
    // ranging from (-1,-1) to (1,1).
    static const GLfloat square_vertices[] = {
        -1.0f, -1.0f,    1.0f, -1.0f,
        -1.0f, 1.0f,     1.0f, 1.0f
    };
    static const GLuint square_indices[] = { 0, 1, 2, 3 };

    // Initialize vertex and element buffers.
    GLuint buf;
    ST.GenBuffers(1, &buf);
    ST.BindBuffer(GL_ARRAY_BUFFER, buf);
    ST.BufferData(GL_ARRAY_BUFFER,
                  sizeof(square_vertices), square_vertices, GL_STATIC_DRAW);
    ST.vertex_buffer = buf;

    ST.GenBuffers(1, &buf);
    ST.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf);
    ST.BufferData(GL_ELEMENT_ARRAY_BUFFER,
                  sizeof(square_indices), square_indices, GL_STATIC_DRAW);
    ST.element_buffer = buf;

    ST.BindBuffer(GL_ARRAY_BUFFER, 0);
    ST.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

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
    ST.DeleteShader(ST.vertex_shader_id);
    ST.DeleteFramebuffers(1, &ST.framebuffer_id);

    ModuleState *mod = ST.state_list;
    while (mod) {
        delete_kernel(user_context, mod->kernel);
        ModuleState *next = mod->next;
        free(mod);
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
#ifdef DEBUG
    if (freed_textures > 0) {
        halide_printf(user_context,
            "halide_opengl_release: deleted %d dangling texture(s).\n",
            freed_textures);
    }
#endif

    ST.DeleteBuffers(1, &ST.vertex_buffer);
    ST.DeleteBuffers(1, &ST.element_buffer);

    ST.vertex_shader_id = 0;
    ST.framebuffer_id = 0;
    ST.vertex_buffer = 0;
    ST.element_buffer = 0;
    ST.textures = NULL;
    ST.state_list = NULL;
    ST.initialized = false;
}

// Determine OpenGL texture format and channel type for a given buffer_t.
WEAK bool get_texture_format(void *user_context,
                               buffer_t *buf,
                               GLint *format,
                               GLint *type) {
    if (buf->extent[2] <= 1) {
        *format = GL_LUMINANCE;
    } else if (buf->extent[2] == 3) {
        *format = GL_RGB;
    } else if (buf->extent[2] == 4) {
        *format = GL_RGBA;
    } else {
        halide_error(user_context, "Only 1, 3, or 4 color channels are supported");
        return false;
    }

    if (buf->elem_size == 1) {
        *type = GL_UNSIGNED_BYTE;
    } else if (buf->elem_size == 2) {
        *type = GL_UNSIGNED_SHORT;
    } else {
        halide_error(user_context, "Only uint8 and uint16 textures are supported");
        return false;
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
        halide_error(user_context, "Invalid buffer");
        return 1;
    }

    // If the texture was already created by the host application, check that
    // it has the correct format. Otherwise, allocate and set up an
    // appropriate texture.
    GLuint tex = get_texture_id(buf);
    bool halide_allocated = false;
    GLint format = 0;
    GLint width, height;
    if (tex != 0) {
#ifdef HAVE_GLES3
        ST.BindTexture(GL_TEXTURE_2D, tex);
        ST.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        ST.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        CHECK_GLERROR(1);
        if (width < buf->extent[0] || height < buf->extent[1]) {
#ifdef DEBUG
            halide_printf(user_context, "Texture size: %dx%d, buffer size: %dx%d\n",
                          width, height, buf->extent[0], buf->extent[1]);
#endif
            halide_error(user_context, "Existing texture is smaller than buffer");
            return 1;
        }
#endif
    } else {
        if (buf->extent[3] > 1) {
            halide_error(user_context, "3D textures are not supported");
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
        GLint type = GL_UNSIGNED_BYTE;
        if (!get_texture_format(user_context, buf, &format, &type)) {
            halide_error(user_context, "Invalid texture format\n");
            return 1;
        }
        width = buf->extent[0];
        height = buf->extent[1];

        ST.TexImage2D(GL_TEXTURE_2D, 0, format,
                      width, height, 0, format, type, NULL);
        CHECK_GLERROR(1);

        buf->dev = tex;
        halide_allocated = true;
#ifdef DEBUG
        halide_printf(user_context, "Allocated texture %d of size %d x %d\n", tex, width, height);
#endif

        ST.BindTexture(GL_TEXTURE_2D, 0);
    }

    // Record main information about texture and remember it for later. In
    // halide_opengl_dev_run we are only given the texture ID and not the full
    // buffer_t, so we copy the interesting information here.  (There can be
    // multiple dev_malloc calls for the same buffer_t. Only record texture
    // information once.)
    if (!find_texture(tex)) {
        TextureInfo *texinfo = (TextureInfo*)malloc(sizeof(TextureInfo));
        texinfo->id = tex;
        for (int i=0; i<3; i++) {
            texinfo->min[i] = buf->min[i];
            texinfo->extent[i] = buf->extent[i];
        }
        texinfo->format = format;
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
        halide_error(user_context, "Internal error: texture not found.\n");
        return 1;
    }

    // Delete texture if it was allocated by us.
    if (texinfo->halide_allocated) {
#ifdef DEBUG
        halide_printf(user_context, "Deleting texture %d\n", tex);
#endif
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
    // TODO: handle error
    if (int error = halide_opengl_init(user_context)) {
        return error;
    }

    ModuleState **state = (ModuleState **)state_ptr;
    ModuleState *mod = *state;
    if (!mod) {
        mod = (ModuleState *)malloc(sizeof(ModuleState));
        mod->kernel = NULL;
        mod->next = ST.state_list;
        ST.state_list = mod;
    }
    *state = mod;

    if (!mod->kernel) {
        KernelInfo *kernel = create_kernel(user_context, src, size);
        if (!kernel) {
            halide_error(user_context, "Invalid kernel\n");
            return -1;
        }

        // Compile shader
        kernel->shader_id = make_shader(user_context, GL_FRAGMENT_SHADER,
                                        kernel->source, NULL);

        // Link GLSL program
        GLuint program = ST.CreateProgram();
        ST.AttachShader(program, ST.vertex_shader_id);
        ST.AttachShader(program, kernel->shader_id);
        ST.LinkProgram(program);
        GLint status;
        ST.GetProgramiv(program, GL_LINK_STATUS, &status);
        if (!status) {
            halide_printf(user_context, "Could not link GLSL program:\n");
            GLint log_len;
            ST.GetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
            char *log = (char*) malloc(log_len);
            ST.GetProgramInfoLog(program, log_len, NULL, log);
            halide_printf(user_context, "%s", log);
            free(log);
            ST.DeleteProgram(program);
            program = 0;
        }
        kernel->program_id = program;
        mod->kernel = kernel;
    }
    return 0;
}

WEAK int halide_opengl_dev_sync(void *user_context) {
    CHECK_INITIALIZED(1);
    // TODO: glFinish()
    return 0;
}

// This function is called to populate the buffer_t.dev field with a constant
// indicating that the OpenGL object corresponding to the buffer_t is bound by
// the app and not by the halide runtime. For example, the buffer_t may be
// backed by an FBO already bound by the application.
WEAK uint64_t halide_opengl_output_client_bound() {
  return HALIDE_GLSL_CLIENT_BOUND;
}

template <class T>
WEAK void halide_to_interleaved(buffer_t *buf, T *dst, int width, int height,
                                  int channels) {
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
WEAK void interleaved_to_halide(buffer_t *buf, T *src, int width, int height,
                                  int channels) {
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
#ifdef DEBUG
        print_buffer(user_context, buf);
#endif
        halide_error(user_context, "Invalid copy_to_dev operation\n");
        return 1;
    }

    GLuint tex = get_texture_id(buf);
#ifdef DEBUG
    halide_printf(user_context, "halide_copy_to_dev: %d\n", tex);
#endif

    ST.BindTexture(GL_TEXTURE_2D, tex);
    CHECK_GLERROR(1);

    GLint format, type;
    if (!get_texture_format(user_context, buf, &format, &type)) {
        halide_error(user_context, "Invalid texture format\n");
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
        #ifdef DEBUG
        halide_printf(user_context, "Warning: In copy_to_dev, host buffer is not interleaved. Doing slow interleave.\n");
        #endif

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
        halide_printf(user_context, "Setting up GL framebuffer %d failed (%x)\n",
                      ST.framebuffer_id, status);
        ST.BindFramebuffer(GL_FRAMEBUFFER, 0);
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
        #ifdef DEBUG
        print_buffer(user_context, buf);
        #endif
        halide_error(user_context, "Invalid copy_to_host operation: host or dev NULL.\n");
        return 1;
    }

    #ifdef DEBUG
    GLuint tex = get_texture_id(buf);
    halide_printf(user_context, "halide_copy_to_host: %d\n", tex);
    #endif

    GLint format, type;
    if (!get_texture_format(user_context, buf, &format, &type)) {
        halide_error(user_context, "Invalid texture format\n");
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
        #ifdef DEBUG
        halide_printf(user_context,
                      "Warning: In copy_to_host, host buffer is not interleaved. Doing slow deinterleave.\n");
        #endif

        size_t stride = width * buf->extent[2] * buf->elem_size;
        size_t size = height * stride;
        uint8_t *tmp = (uint8_t*)halide_malloc(user_context, size);
        if (!tmp) {
            halide_error(user_context, "halide_malloc failed inside copy_to_host\n");
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


WEAK int halide_opengl_dev_run(
    void *user_context,
    void *state_ptr,
    const char *entry_name,
    int blocksX, int blocksY, int blocksZ,
    int threadsX, int threadsY, int threadsZ,
    int shared_mem_bytes,
    size_t arg_sizes[],
    void *args[]) {
    CHECK_INITIALIZED(1);

    ModuleState *mod = (ModuleState *)state_ptr;
    if (!mod) {
        halide_error(user_context, "Internal error: module state is NULL\n");
        return -1;
    }

    KernelInfo *kernel = mod->kernel;
    if (!kernel) {
        halide_printf(user_context, "Internal error: unknown kernel named '%s'\n", entry_name);
        return 1;
    }

    ST.UseProgram(kernel->program_id);

    Argument *kernel_arg;
    bool bind_render_targets = true;

    // Copy input arguments to corresponding GLSL uniforms.
    GLint num_active_textures = 0;
    kernel_arg = kernel->arguments;
    for (int i = 0; args[i]; i++, kernel_arg = kernel_arg->next) {
        if (!kernel_arg) {
            halide_printf(user_context, "Argument %d: size=%d value=%p\n", i,
                          arg_sizes[i], args[i]);
            halide_error(user_context,
                         "Too many arguments passed to halide_opengl_dev_run");
            return 1;
        }

        if (kernel_arg->kind == ARGKIND_OUTBUF) {
            // Check if the output buffer will be bound by the client instead of
            // the Halide runtime
            GLuint tex = *((GLuint *)args[i]);
            if (tex == (GLuint)HALIDE_GLSL_CLIENT_BOUND) {
                bind_render_targets = false;
            }
            // Outbuf textures are handled explicitly below
            continue;
        } else if (kernel_arg->kind == ARGKIND_INBUF) {
            GLint loc =
                ST.GetUniformLocation(kernel->program_id, kernel_arg->name);
            CHECK_GLERROR(1);
            if (loc == -1) {
                halide_error(user_context, "No sampler defined for input texture.\n");
                return 1;
            }
            GLuint tex = *((GLuint *)args[i]);
            ST.ActiveTexture(GL_TEXTURE0 + num_active_textures);
            ST.BindTexture(GL_TEXTURE_2D, tex);
            ST.Uniform1iv(loc, 1, &num_active_textures);
            num_active_textures++;
            // TODO: check maximum number of active textures
        } else if (kernel_arg->kind == ARGKIND_VAR) {
            GLint loc =
                ST.GetUniformLocation(kernel->program_id, kernel_arg->name);
            CHECK_GLERROR(1);
            if (loc == -1) {
                // Argument was probably optimized away by GLSL compiler.
                continue;
            }

            // Note: small integers are represented as floats in GLSL.
            switch (kernel_arg->type) {
            case ARGTYPE_FLOAT:
                set_float_param(user_context, kernel_arg->name, loc, *(float*)args[i]);
                break;
            case ARGTYPE_BOOL: {
                GLint value = *((bool*)args[i]) ? 1 : 0;
                set_int_param(user_context, kernel_arg->name, loc, value);
                break;
            }
            case ARGTYPE_INT8: {
                GLfloat value = *((int8_t*)args[i]);
                set_float_param(user_context, kernel_arg->name, loc, value);
                break;
            }
            case ARGTYPE_UINT8: {
                GLfloat value = *((uint8_t*)args[i]);
                set_float_param(user_context, kernel_arg->name, loc, value);
                break;
            }
            case ARGTYPE_INT16: {
                GLfloat value = *((int16_t*)args[i]);
                set_float_param(user_context, kernel_arg->name, loc, value);
                break;
            }
            case ARGTYPE_UINT16: {
                GLfloat value = *((uint16_t*)args[i]);
                set_float_param(user_context, kernel_arg->name, loc, value);
                break;
            }
            case ARGTYPE_INT32: {
                GLint value = *((int32_t*)args[i]);
                set_int_param(user_context, kernel_arg->name, loc, value);
                break;
            }
            case ARGTYPE_UINT32: {
                uint32_t value = *((uint32_t*)args[i]);
                GLint signed_value;
                if (value > 0x7fffffff) {
                    halide_error_varargs(user_context,
                                         "GLSL: argument '%' is too large for GLint\n",
                                         kernel_arg->name);
                    return -1;
                }
                signed_value = static_cast<GLint>(value);
                set_int_param(user_context, kernel_arg->name, loc, signed_value);
                break;
            }
            case ARGTYPE_NONE:
                halide_error(user_context, "Unknown kernel argument type");
                return 1;
            }
        }
    }
    if (kernel_arg) {
        halide_error(user_context, "Too few arguments passed to halide_opengl_dev_run");
        return 1;
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
        if (kernel_arg->kind != ARGKIND_OUTBUF) continue;

        // TODO: GL_MAX_COLOR_ATTACHMENTS
        if (num_output_textures >= 1) {
            halide_error(user_context,
                         "OpenGL ES 2.0 only supports one single output texture");
            return 1;
        }

        GLuint tex = *((GLuint*)args[i]);

        // Check to see if the object name is actually a FBO
        if (bind_render_targets) {
#ifdef DEBUG
            halide_printf(user_context, "Output texture %d: %d\n", num_output_textures, tex);
#endif
            ST.FramebufferTexture2D(GL_FRAMEBUFFER,
                                    GL_COLOR_ATTACHMENT0 + num_output_textures,
                                    GL_TEXTURE_2D, tex, 0);
            CHECK_GLERROR(1);
        }

        TextureInfo *texinfo = find_texture(tex);
        if (!texinfo) {
            halide_error(user_context, "Undefined output texture");
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
        halide_printf(user_context, "Warning: kernel has no output\n");
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
            halide_printf(user_context, "Setting up GL framebuffer %d failed (%x)\n",
                          ST.framebuffer_id, status);
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

    // Setup viewport
    ST.Viewport(0, 0, output_extent[0], output_extent[1]);

    // Execute shader
    GLint position = ST.GetAttribLocation(kernel->program_id, "position");
    ST.BindBuffer(GL_ARRAY_BUFFER, ST.vertex_buffer);
    ST.VertexAttribPointer(position,
                           2,
                           GL_FLOAT,
                           GL_FALSE,    // normalized?
                           sizeof(GLfloat)*2,
                           NULL);
    ST.EnableVertexAttribArray(position);
    ST.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ST.element_buffer);
    ST.DrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_INT, NULL);
    CHECK_GLERROR(1);
    ST.DisableVertexAttribArray(position);

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
    return 0;
}

}}} // namespace Halide::Runtime::Internal

//  Create wrappers that satisfy old naming conventions

extern "C" {
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
                          size_t arg_sizes[], void *args[]) {
    return halide_opengl_dev_run(user_context, state_ptr,
                                 entry_name,
                                 blocksX, blocksY, blocksZ,
                                 threadsX, threadsY, threadsY,
                                 shared_mem_bytes,
                                 arg_sizes, args);
}

WEAK int halide_dev_sync(void *user_context) {
    return halide_opengl_dev_sync(user_context);
}

WEAK int halide_init_kernels(void *user_context, void **state_ptr,
                             const char *src, int size) {
    return halide_opengl_init_kernels(user_context, state_ptr, src, size);
}
}
