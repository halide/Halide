// Implementation note: all function that directly or indirectly access the
// runtime state in halide_opengl_state must be declared as WEAK, otherwise
// the behavior at runtime is undefined.

#include "mini_stdint.h"
#include "../buffer_t.h"
#include "HalideRuntime.h"

#include "mini_string.h"
#include "mini_opengl.h"

#define EXPORT extern "C" WEAK

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

enum ArgumentKind {
    ARGKIND_NONE,
    ARGKIND_VAR,
    ARGKIND_INBUF,
    ARGKIND_OUTBUF
};

enum ArgumentType {
    ARGTYPE_NONE,
    ARGTYPE_FLOAT,
    ARGTYPE_INT,
    ARGTYPE_UINT8,
    ARGTYPE_UINT16
};

struct HalideOpenGLArgument {
    char *name;
    ArgumentKind kind;
    ArgumentType type;
    HalideOpenGLArgument *next;
};

struct HalideOpenGLKernel {
    char *source;
    HalideOpenGLArgument *arguments;
    GLuint shader_id;
    GLuint program_id;
};

// Information about each known texture.
struct HalideOpenGLTexture {
    GLuint id;
    GLint min[4];
    GLint extent[4];
    GLenum format;                      // internal format: GL_RGBA, ...
    bool halide_allocated;              // allocated by us or host app?
    HalideOpenGLTexture *next;
};

struct ModuleState {
    HalideOpenGLKernel *kernel;
    ModuleState *next;
};

// All persistent state maintained by the runtime.
struct HalideOpenGLState {
    bool initialized;

    // Various objects shared by all filter kernels
    GLuint vertex_shader_id;
    GLuint framebuffer_id;
    GLuint vertex_buffer;
    GLuint element_buffer;

    // A list of all textures that are still active
    HalideOpenGLTexture *textures;

    ModuleState *state_list;

    // Declare pointers used OpenGL functions
#define GLFUNC(PTYPE,VAR) PTYPE VAR
    USED_GL_FUNCTIONS;
#undef GLFUNC
};

// ---------- Static variables ----------

WEAK HalideOpenGLState halide_opengl_state;

static const char *vertex_shader_src =
    "attribute vec2 position;\n"
    "varying vec2 pixcoord;\n"
    "uniform ivec2 output_min;\n"
    "uniform ivec2 output_extent;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "    vec2 texcoord = 0.5 * position + 0.5;\n"
    "    pixcoord = texcoord * vec2(output_extent.xy) + vec2(output_min.xy);\n"
    "}\n";

static const char kernel_marker[] = "/// KERNEL ";
static const char input_marker[] = "/// IN_BUFFER ";
static const char output_marker[] = "/// OUT_BUFFER ";
static const char var_marker[] = "/// VAR ";

// ---------- Macros ----------

// Convenience macro for accessing state of the OpenGL runtime
#define ST halide_opengl_state

// Ensure that OpenGL runtime is correctly initialized. Used in all public API
// functions.
#define CHECK_INITIALIZED(ERRORCODE)				\
    if (!ST.initialized) {					\
	halide_error(user_context, "OpenGL runtime not initialized.\n");	\
	return ERRORCODE;					\
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
        GLenum err = halide_opengl_state.GetError();                    \
        if (err != GL_NO_ERROR) {                                       \
            LOG_GLERROR(err);                                           \
            halide_error(user_context, "OpenGL error");                 \
            return ERRORCODE;                                           \
        }} while (0)


// ---------- Helper functions ----------

extern "C" void *malloc(size_t);
extern "C" void free(void*);

static char *strndup(const char *s, size_t n) {
    char *p = (char*)malloc(n+1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static GLuint get_texture_id(buffer_t *buf) {
    return buf->dev & 0xffffffff;
}

static void print_buffer(void *user_context, buffer_t *buf) {
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

WEAK GLuint halide_opengl_make_shader(void *user_context, GLenum type,
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
static const char *match_prefix(const char *s, const char *prefix) {
    if (0 == strncmp(s, prefix, strlen(prefix))) {
        return s + strlen(prefix);
    }
    return NULL;
}

// Parse declaration of the form "type name" and construct
// matching HalideOpenGLArgument.
static HalideOpenGLArgument *parse_argument(void *user_context, const char *src,
                                            const char *end) {
    const char *name;
    ArgumentType type = ARGTYPE_NONE;
    if ((name = match_prefix(src, "float "))) {
        type = ARGTYPE_FLOAT;
    } else if ((name = match_prefix(src, "int "))) {
        type = ARGTYPE_INT;
    } else if ((name = match_prefix(src, "uint8 "))) {
        type = ARGTYPE_UINT8;
    } else if ((name = match_prefix(src, "uint16 "))) {
	type = ARGTYPE_UINT16;
    }
    if (type == ARGTYPE_NONE) {
        halide_error(user_context, "Internal error: argument type not supported");
        return NULL;
    }

    HalideOpenGLArgument *arg =
        (HalideOpenGLArgument *)malloc(sizeof(HalideOpenGLArgument));
    arg->name = strndup(name, end - name);
    arg->type = type;
    arg->kind = ARGKIND_NONE;
    arg->next = 0;
    return arg;
}

// Create HalideOpenGLKernel for a piece of GLSL code
static HalideOpenGLKernel *create_kernel(void *user_context, const char *src, int size) {
    HalideOpenGLKernel *kernel =
        (HalideOpenGLKernel *)malloc(sizeof(HalideOpenGLKernel));

    kernel->source = strndup(src, size);
    kernel->arguments = NULL;
    kernel->shader_id = 0;
    kernel->program_id = 0;

    #ifdef DEBUG
    halide_printf(user_context, "Compiling GLSL kernel:\n%s\n",
                  kernel->source);
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
            if (HalideOpenGLArgument *arg =
                parse_argument(user_context, args, next_line - 1)) {
                arg->kind = ARGKIND_VAR;
                arg->next = kernel->arguments;
                kernel->arguments = arg;
            }
        } else if ((args = match_prefix(line, input_marker))) {
            if (HalideOpenGLArgument *arg =
                parse_argument(user_context, args, next_line - 1)) {
                arg->kind = ARGKIND_INBUF;
                arg->next = kernel->arguments;
                kernel->arguments = arg;
            }
        } else if ((args = match_prefix(line, output_marker))) {
            if (HalideOpenGLArgument *arg =
                parse_argument(user_context, args, next_line - 1)) {
                arg->kind = ARGKIND_OUTBUF;
                arg->next = kernel->arguments;
                kernel->arguments = arg;
            }
        } else {
            // Stop parsing if we encounter something we don't recognize
            break;
        }
        line = next_line;
    }

    // Arguments are currently in reverse order, flip the list.
    HalideOpenGLArgument *cur = kernel->arguments;
    kernel->arguments = NULL;
    while (cur) {
        HalideOpenGLArgument *next = cur->next;
        cur->next = kernel->arguments;
        kernel->arguments = cur;
        cur = next;
    }

    return kernel;
}

// Delete all data associated with a kernel. Also release associated OpenGL
// shader and program.
WEAK void halide_opengl_delete_kernel(void *user_context, HalideOpenGLKernel *kernel) {
    ST.DeleteProgram(kernel->program_id);
    ST.DeleteShader(kernel->shader_id);

    HalideOpenGLArgument *arg = kernel->arguments;
    while (arg) {
        HalideOpenGLArgument *next = arg->next;
        free(arg);
        arg = next;
    }
    free(kernel);
}

// Initialize the runtime, in particular all fields in halide_opengl_state.
EXPORT int halide_opengl_init(void *user_context) {
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
    ST.vertex_shader_id = halide_opengl_make_shader(user_context,
        GL_VERTEX_SHADER, vertex_shader_src, NULL);
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
EXPORT void halide_opengl_release(void *user_context) {
    if (!ST.initialized) return;

#ifdef DEBUG
    halide_printf(user_context, "halide_opengl_release\n");
#endif
    ST.DeleteShader(ST.vertex_shader_id);
    ST.DeleteFramebuffers(1, &ST.framebuffer_id);

    ModuleState *mod = ST.state_list;
    while (mod) {
        halide_opengl_delete_kernel(user_context, mod->kernel);
        ModuleState *next = mod->next;
        free(mod);
        mod = next;
    }

    // Delete all textures that were allocated by us.
    HalideOpenGLTexture *tex = ST.textures;
    int freed_textures = 0;
    while (tex) {
        HalideOpenGLTexture *next = tex->next;
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
static bool get_texture_format(void *user_context,
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


EXPORT HalideOpenGLTexture *halide_opengl_find_texture(GLuint tex) {
    HalideOpenGLTexture *texinfo = ST.textures;
    while (texinfo && texinfo->id != tex) {
        texinfo = texinfo->next;
    }
    return texinfo;
}

// Allocate a new texture matching the dimension and color format of the
// specified buffer.
EXPORT int halide_opengl_dev_malloc(void *user_context, buffer_t *buf) {
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
    if (!halide_opengl_find_texture(tex)) {
        HalideOpenGLTexture *texinfo =
            (HalideOpenGLTexture*)malloc(sizeof(HalideOpenGLTexture));
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
EXPORT int halide_opengl_dev_free(void *user_context, buffer_t *buf) {
    CHECK_INITIALIZED(1);

    GLuint tex = get_texture_id(buf);
    if (tex == 0) {
        return 0;
    }

    // Look up corresponding HalideOpenGLTexture and unlink it from the list.
    HalideOpenGLTexture **ptr = &ST.textures;
    HalideOpenGLTexture *texinfo = *ptr;
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
EXPORT int halide_opengl_init_kernels(void *user_context, void **state_ptr,
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
        HalideOpenGLKernel *kernel = create_kernel(user_context, src, size);
        if (!kernel) {
            halide_error(user_context, "Invalid kernel\n");
            return -1;
        }

        // Compile shader
        kernel->shader_id = halide_opengl_make_shader(user_context, GL_FRAGMENT_SHADER,
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

EXPORT int halide_opengl_dev_sync(void *user_context) {
    CHECK_INITIALIZED(1);
    // TODO: glFinish()
    return 0;
}

template <class T>
static void halide_to_interleaved(buffer_t *buf, T *dst, int width, int height,
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
static void interleaved_to_halide(buffer_t *buf, T *src, int width, int height,
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
EXPORT int halide_opengl_copy_to_dev(void *user_context, buffer_t *buf) {
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

    bool is_interleaved =
        (buf->stride[2] == 1 && buf->stride[0] == buf->extent[2]);
    if (is_interleaved) {
        ST.PixelStorei(GL_UNPACK_ROW_LENGTH, buf->extent[1]);
        ST.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        ST.TexSubImage2D(GL_TEXTURE_2D, 0,
                         0, 0, width, height,
                         format, type, buf->host);
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
static int GetPixels(void *user_context, buffer_t *buf, GLint format, GLint type, void *dest) {
#if 0
    // glGetImage isn't supported in OpenGL ES, so this path is currently
    // commented out.
    ST.BindTexture(GL_TEXTURE_2D, tex);
    ST.GetTexImage(GL_TEXTURE_2D, 0, format, type, dest);
    ST.BindTexture(GL_TEXTURE_2D, 0);
#else
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
#endif
    return 0;
}


// Copy image data from texture back to host memory.
EXPORT int halide_opengl_copy_to_host(void *user_context, buffer_t *buf) {
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

    GLuint tex = get_texture_id(buf);
    #ifdef DEBUG
    halide_printf(user_context, "halide_copy_to_host: %d\n", tex);
    #endif

    GLint format, type;
    if (!get_texture_format(user_context, buf, &format, &type)) {
        halide_error(user_context, "Invalid texture format\n");
        return 1;
    }
    GLint width = buf->extent[0], height = buf->extent[1];
    bool is_interleaved = (buf->stride[2] == 1 && buf->stride[0] == buf->extent[2]);
    if (is_interleaved) {
        // TODO: GL_UNPACK_ROW_LENGTH
        ST.PixelStorei(GL_PACK_ROW_LENGTH, buf->extent[1]);
        ST.PixelStorei(GL_PACK_ALIGNMENT, 1);
        GetPixels(user_context, buf, format, type, buf->host);
    } else {
        #ifdef DEBUG
        halide_printf(user_context, "Warning: In copy_to_host, host buffer is not interleaved. Doing slow deinterleave.\n");
        #endif

        size_t size = width * height * buf->extent[2] * buf->elem_size;
        uint8_t *tmp = (uint8_t*)halide_malloc(user_context, size);
        if (!tmp) {
            halide_error(user_context, "halide_malloc failed inside copy_to_host\n");
            return -1;
        }

        ST.PixelStorei(GL_PACK_ALIGNMENT, 1);
        if (int err = GetPixels(user_context, buf, format, type, tmp)) {
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


EXPORT int halide_opengl_dev_run(
    void *user_context,
    void *state_ptr,
    const char *entry_name,
    int blocksX, int blocksY, int blocksZ,
    int threadsX, int threadsY, int threadsZ,
    int shared_mem_bytes,
    size_t arg_sizes[],
    void *args[])
{
    CHECK_INITIALIZED(1);

    ModuleState *mod = (ModuleState *)state_ptr;
    if (!mod) {
        halide_error(user_context, "Internal error: module state is NULL\n");
        return -1;
    }

    HalideOpenGLKernel *kernel = mod->kernel;
    if (!kernel) {
        halide_printf(user_context, "Internal error: unknown kernel named '%s'\n", entry_name);
        return 1;
    }

    ST.UseProgram(kernel->program_id);

    HalideOpenGLArgument *kernel_arg;

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

            switch (kernel_arg->type) {
            case ARGTYPE_INT:
                #ifdef DEBUG
                halide_printf(user_context, "Setting int %s = %d (loc=%d)\n",
                              kernel_arg->name, *((GLint *)args[i]), loc);
                #endif
                ST.Uniform1iv(loc, 1, (GLint *)args[i]);
                break;
            case ARGTYPE_FLOAT: {
                #ifdef DEBUG
                halide_printf(user_context, "Setting float %s = %g (loc=%d)\n",
                              kernel_arg->name, *((GLfloat *)args[i]), loc);
                #endif
                ST.Uniform1fv(loc, 1, (GLfloat *)args[i]);
                break;
            }
            case ARGTYPE_NONE:
            default:
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
    ST.BindFramebuffer(GL_FRAMEBUFFER, ST.framebuffer_id);
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
#ifdef DEBUG
        halide_printf(user_context, "Output texture %d: %d\n", num_output_textures, tex);
#endif
        ST.FramebufferTexture2D(GL_FRAMEBUFFER,
                                GL_COLOR_ATTACHMENT0 + num_output_textures,
                                GL_TEXTURE_2D, tex, 0);
        CHECK_GLERROR(1);

        HalideOpenGLTexture *texinfo = halide_opengl_find_texture(tex);
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
        CHECK_GLERROR(1);
        free(draw_buffers);
    }

    // Check that framebuffer is set up correctly
    GLenum status = ST.CheckFramebufferStatus(GL_FRAMEBUFFER);
    CHECK_GLERROR(1);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        halide_printf(user_context, "Setting up GL framebuffer %d failed (%x)\n",
                      ST.framebuffer_id, status);
        // TODO: cleanup
        return 1;
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
    ST.BindFramebuffer(GL_FRAMEBUFFER, 0);
    ST.BindBuffer(GL_ARRAY_BUFFER, 0);
    ST.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return 0;
}

//  Create wrappers that satisfy old naming conventions

EXPORT void halide_release(void *user_context) {
    halide_opengl_release(user_context);
}

EXPORT int halide_dev_malloc(void *user_context, buffer_t *buf) {
    return halide_opengl_dev_malloc(user_context, buf);
}

EXPORT int halide_dev_free(void *user_context, buffer_t *buf) {
    return halide_opengl_dev_free(user_context, buf);
}

EXPORT int halide_copy_to_host(void *user_context, buffer_t *buf) {
    return halide_opengl_copy_to_host(user_context, buf);
}

EXPORT int halide_copy_to_dev(void *user_context, buffer_t *buf) {
    return halide_opengl_copy_to_dev(user_context, buf);
}

EXPORT int halide_dev_run(void *user_context,
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

EXPORT int halide_dev_sync(void *user_context) {
    return halide_opengl_dev_sync(user_context);
}

EXPORT int halide_init_kernels(void *user_context, void **state_ptr,
                               const char *src, int size) {
    return halide_opengl_init_kernels(user_context, state_ptr, src, size);
}
