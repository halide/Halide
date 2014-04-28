#include "mini_stdint.h"
#include "../buffer_t.h"
#include "HalideRuntime.h"

#include "posix_allocator.cpp"
#include "posix_io.cpp"
#include "posix_error_handler.cpp"
#include "mini_string.h"
#include "mini_opengl.h"

#define EXPORT extern "C" WEAK

// This function must be provided by the host environment to retrieve pointers
// to OpenGL API functions.
extern "C" void *halide_opengl_get_proc_address(const char *name);

// List of all OpenGL functions used by the runtime. The list is used to
// declare and initialize the dispatch table in OpenGLState below.
#define USED_GL_FUNCTIONS                                               \
    GLFUNC(PFNGLDELETETEXTURESPROC, DeleteTextures);                    \
    GLFUNC(PFNGLGENTEXTURESPROC, GenTextures);                          \
    GLFUNC(PFNGLBINDTEXTUREPROC, BindTexture);                          \
    GLFUNC(PFNGLGETERRORPROC, GetError);                                \
    GLFUNC(PFNGLMATRIXMODEPROC, MatrixMode);                            \
    GLFUNC(PFNGLLOADIDENTITYPROC, LoadIdentity);                        \
    GLFUNC(PFNGLORTHOPROC, Ortho);                                      \
    GLFUNC(PFNGLVIEWPORTPROC, Viewport);                                \
    GLFUNC(PFNGLGENBUFFERSPROC, GenBuffers);                            \
    GLFUNC(PFNGLDELETEBUFFERSPROC, DeleteBuffers);                      \
    GLFUNC(PFNGLBINDBUFFERPROC, BindBuffer);                            \
    GLFUNC(PFNGLBUFFERDATAPROC, BufferData);                            \
    GLFUNC(PFNGLTEXPARAMETERIPROC, TexParameteri);                      \
    GLFUNC(PFNGLTEXIMAGE2DPROC, TexImage2D);                            \
    GLFUNC(PFNGLGETTEXIMAGEPROC, GetTexImage);                          \
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
    GLFUNC(PFNGLDRAWBUFFERSPROC, DrawBuffers);                          \
    GLFUNC(PFNGLGETATTRIBLOCATIONPROC, GetAttribLocation);              \
    GLFUNC(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer);          \
    GLFUNC(PFNGLDRAWELEMENTSPROC, DrawElements);                        \
    GLFUNC(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray);  \
    GLFUNC(PFNGLDISABLEVERTEXATTRIBARRAYPROC, DisableVertexAttribArray); \
    GLFUNC(PFNGLGETTEXLEVELPARAMETERIVPROC, GetTexLevelParameteriv);    \
    GLFUNC(PFNGLPIXELSTOREIPROC, PixelStorei)

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
    ARGTYPE_UINT8
};

struct HalideOpenGLArgument {
    char *name;
    ArgumentKind kind;
    ArgumentType type;
    HalideOpenGLArgument *next;
};

struct HalideOpenGLKernel {
    char* source;
    char* name;
    HalideOpenGLArgument *arguments;
    GLuint shader_id;
    GLuint program_id;
    HalideOpenGLKernel *next;
};

// Information about each known texture.
struct HalideOpenGLTexture {
    GLuint id;
    GLint min[4];
    GLint extent[4];
    GLenum format;                      // internal format: GL_RGBA, ...
    bool halide_allocated;              // allocated by us or host app?
    HalideOpenGLTexture* next;
};

// All persistant state maintained by the runtime.
struct HalideOpenGLState {
    bool initialized;

    // Various objects shared by all filter kernels
    GLuint vertex_shader_id;
    GLuint framebuffer_id;
    GLuint vertex_buffer;
    GLuint element_buffer;

    // A list of all defined kernels
    HalideOpenGLKernel* kernels;

    // A list of all textures that are still active
    HalideOpenGLTexture* textures;

    // Declare pointers used OpenGL functions
#define GLFUNC(PTYPE,VAR) PTYPE VAR
    USED_GL_FUNCTIONS;
#undef GLFUNC
};

// ---------- Static variables ----------

WEAK HalideOpenGLState halide_opengl_state;

static const char* vertex_shader_src =
    "#version 120\n"
    "attribute vec2 position;\n"
    "varying vec2 pixcoord;\n"
    "uniform ivec2 output_min;\n"
    "uniform ivec2 output_extent;\n"
    "void main() {\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "    vec2 texcoord = 0.5 * position + 0.5;\n"
    "    pixcoord = floor(texcoord * output_extent) + output_min;\n"
    "}\n";

static const char kernel_marker[] = "/// KERNEL ";
static const char input_marker[] = "/// IN_BUFFER ";
static const char output_marker[] = "/// OUT_BUFFER ";
static const char var_marker[] = "/// VAR ";

// Vertex coordinates for unit square
static const GLfloat square_vertices[] = {
    -1.0f, -1.0f,
    1.0f, -1.0f,
    -1.0f, 1.0f,
    1.0f, 1.0f
};

// Order of vertices in vertex_buffer_data for triangle strip forming the unit
// square.
static const GLuint square_indices[] = { 0, 1, 2, 3 };

// ---------- Macros ----------

// Convenience macro for accessing state of the OpenGL runtime
#define ST halide_opengl_state

// Ensure that OpenGL runtime is correctly initialized. Used in all public API
// functions.
#define CHECK_INITIALIZED(ERRORCODE)				\
    if (!ST.initialized) {					\
	halide_error(uctx, "OpenGL runtime not initialized.");	\
	return ERRORCODE;					\
    }

// Macro for error checking.
#ifdef DEBUG
#define LOG_GLERROR(ERR)                                        \
    halide_printf(uctx,                                         \
                  "%s:%d: OpenGL error 0x%04x\n",               \
                  __FILE__, __LINE__, (ERR))
#else
#define LOG_GLERROR(ERR)
#endif

#define CHECK_GLERROR(ERRORCODE) {                              \
        GLenum err;                                             \
        if ((err = ST.GetError()) != GL_NO_ERROR) {             \
            LOG_GLERROR(err);                                   \
            halide_error(uctx, "OpenGL error");                 \
            return ERRORCODE;                                   \
        }}


// ---------- Helper functions ----------

extern "C" void *malloc(size_t);
extern "C" void free(void*);

// Note: all function that directly or indirectly access the runtime state in
// halide_opengl_state must be declared as WEAK, otherwise the behavior at
// runtime is undefined.

static char* strndup(const char* s, size_t n) {
    char* p = (char*)malloc(n+1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static GLuint get_texture_id(buffer_t* buf) {
    return buf->dev & 0xffffffff;
}


static void print_buffer(void *uctx, buffer_t *buf) {
    halide_printf(uctx, "  dev: %ul\n", buf->dev);
    halide_printf(uctx, "  host: %p\n", buf->host);
    halide_printf(uctx, "  extent: %d %d %d %d\n",
                  buf->extent[0], buf->extent[1], buf->extent[2], buf->extent[3]);
    halide_printf(uctx, "  stride: %d %d %d %d\n",
                  buf->stride[0], buf->stride[1], buf->stride[2], buf->stride[3]);
    halide_printf(uctx, "  min: %d %d %d %d\n",
                  buf->min[0], buf->min[1], buf->min[2], buf->min[3]);
    halide_printf(uctx, "  elem_size: %d\n", buf->elem_size);
    halide_printf(uctx, "  host_dirty: %d, dev_dirty: %d\n",
                  buf->host_dirty, buf->dev_dirty);
}


WEAK GLuint halide_opengl_make_shader(void* uctx, GLenum type,
                                      const char* source, GLint* length) {
    GLuint shader = ST.CreateShader(type);
    ST.ShaderSource(shader, 1, (const GLchar **)&source, length);
    ST.CompileShader(shader);

    GLint shader_ok = 0;
    ST.GetShaderiv(shader, GL_COMPILE_STATUS, &shader_ok);
    if (!shader_ok) {
        halide_printf(uctx, "Could not compile shader:\n");
        GLint log_len;
        ST.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        char* log = (char*) malloc(log_len);
        ST.GetShaderInfoLog(shader, log_len, NULL, log);
        halide_printf(uctx, "%s", log);
        free(log);
        ST.DeleteShader(shader);
        return 0;
    }
    return shader;
}

// Check whether string starts with a given prefix.
// Returns pointer to character after matched prefix if successful or NULL.
static const char* match_prefix(const char *s, const char* prefix) {
    if (0 == strncmp(s, prefix, strlen(prefix)))
        return s + strlen(prefix);
    return NULL;
}

// Parse declaration of the form "type name" and construct
// matching HalideOpenGLArgument.
static HalideOpenGLArgument *parse_argument(void *uctx, const char *src,
                                            const char *end) {
    const char *name;
    ArgumentType type = ARGTYPE_NONE;
    if ((name = match_prefix(src, "float "))) {
        type = ARGTYPE_FLOAT;
    } else if ((name = match_prefix(src, "int "))) {
        type = ARGTYPE_INT;
    } else if ((name = match_prefix(src, "uint8 "))) {
        type = ARGTYPE_UINT8;
    }
    if (type == ARGTYPE_NONE) {
        halide_error(uctx, "Internal error: argument type not supported");
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
static HalideOpenGLKernel* create_kernel(void* uctx, const char* src, int size) {
    HalideOpenGLKernel* kernel =
        (HalideOpenGLKernel*)malloc(sizeof(HalideOpenGLKernel));

    kernel->source = strndup(src, size);
    kernel->name = NULL;
    kernel->arguments = NULL;
    kernel->shader_id = 0;
    kernel->program_id = 0;
    kernel->next = NULL;

    // Parse initial comment block
    const char *line = kernel->source;
    while (*line) {
        const char *next_line = strchr(line, '\n') + 1;
        if (!next_line)
            next_line = line + size;

        const char* args;
        if ((args = match_prefix(line, kernel_marker))) {
            kernel->name = strndup(args, next_line - args - 1);
        } else if ((args = match_prefix(line, var_marker))) {
            if (HalideOpenGLArgument *arg =
                    parse_argument(uctx, args, next_line - 1)) {
                arg->kind = ARGKIND_VAR;
                arg->next = kernel->arguments;
                kernel->arguments = arg;
            }
        } else if ((args = match_prefix(line, input_marker))) {
            if (HalideOpenGLArgument *arg =
                    parse_argument(uctx, args, next_line - 1)) {
                arg->kind = ARGKIND_INBUF;
                arg->next = kernel->arguments;
                kernel->arguments = arg;
            }
        } else if ((args = match_prefix(line, output_marker))) {
            if (HalideOpenGLArgument *arg =
                    parse_argument(uctx, args, next_line - 1)) {
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
    if (!kernel->name) {
        halide_error(uctx, "Internal error: kernel name not specified");
        return NULL;
    }

    // Arguments are currently in reverse order, flip the list.
    HalideOpenGLArgument* cur = kernel->arguments;
    kernel->arguments = NULL;
    while (cur) {
        HalideOpenGLArgument* next = cur->next;
        cur->next = kernel->arguments;
        kernel->arguments = cur;
        cur = next;
    }

    return kernel;
}

// Delete all data associated with a kernel. Also release associated OpenGL
// shader and program.
WEAK void halide_opengl_delete_kernel(void* uctx, HalideOpenGLKernel* kernel) {
    ST.DeleteProgram(kernel->program_id);
    ST.DeleteShader(kernel->shader_id);

    HalideOpenGLArgument *arg = kernel->arguments;
    while (arg) {
        HalideOpenGLArgument *next = arg->next;
        free(arg);
        arg = next;
    }
    free(kernel->name);
    free(kernel);
}

// Find a kernel by name. Return NULL if not found.
WEAK HalideOpenGLKernel* halide_opengl_find_kernel(const char* name) {
    for (HalideOpenGLKernel* cur = ST.kernels; cur; cur = cur->next) {
        if (0 == strcmp(cur->name, name)) {
            return cur;
        }
    }
    return NULL;
}


// Initialize the runtime, in particular all fields in halide_opengl_state.
EXPORT void halide_opengl_init(void* uctx) {
    if (ST.initialized)
        return;

    // Initialize pointers to OpenGL functions.
#define GLFUNC(TYPE, VAR)                                               \
    ST.VAR = (TYPE)halide_opengl_get_proc_address("gl" #VAR);           \
    if (!ST.VAR) {                                                      \
        halide_printf(uctx, "Could not load function pointer for %s\n", "gl" #VAR); \
        return;                                                         \
    }
    USED_GL_FUNCTIONS;
#undef GLFUNC

    ST.kernels = NULL;
    ST.textures = NULL;

    // Initialize all OpenGL objects that are shared between kernels.
    ST.GenFramebuffers(1, &ST.framebuffer_id);
    CHECK_GLERROR();

    ST.vertex_shader_id = halide_opengl_make_shader(uctx,
        GL_VERTEX_SHADER, vertex_shader_src, NULL);
    if (ST.vertex_shader_id == 0) {
	halide_error(uctx, "Failed to create vertex shader");
	return;
    }

    GLuint buf;
    ST.GenBuffers(1, &buf);
    ST.BindBuffer(GL_ARRAY_BUFFER, buf);
    ST.BufferData(GL_ARRAY_BUFFER,
                  sizeof(square_vertices), square_vertices, GL_STATIC_DRAW);
    CHECK_GLERROR();
    ST.vertex_buffer = buf;

    ST.GenBuffers(1, &buf);
    ST.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf);
    ST.BufferData(GL_ELEMENT_ARRAY_BUFFER,
                  sizeof(square_indices), square_indices, GL_STATIC_DRAW);
    CHECK_GLERROR();
    ST.element_buffer = buf;

    ST.initialized = true;
}

// Release all data allocated by the runtime.
//
// The OpenGL context itself is generally managed by the host application, so
// we leave it untouched.
EXPORT void halide_opengl_release(void* uctx) {
    CHECK_INITIALIZED();
    ST.DeleteShader(ST.vertex_shader_id);
    ST.DeleteFramebuffers(1, &ST.framebuffer_id);

    HalideOpenGLKernel* cur = ST.kernels;
    while (cur) {
        HalideOpenGLKernel* next = cur->next;
        halide_opengl_delete_kernel(uctx, cur);
        cur = next;
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
        halide_printf(uctx,
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
    ST.kernels = NULL;
    ST.textures = NULL;
    ST.initialized = false;
}

// Determine OpenGL texture format and channel type for a given buffer_t.
static bool get_texture_format(void *uctx,
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
        halide_error(uctx, "Only 1, 3, or 4 color channels are supported");
        return false;
    }

    if (buf->elem_size == 1) {
        *type = GL_UNSIGNED_BYTE;
    } else if (buf->elem_size == 2) {
        *type = GL_UNSIGNED_SHORT;
    } else {
        halide_error(uctx, "Only uint8 and uint16 textures are supported");
        return false;
    }
    return true;
}


// Allocate a new texture matching the dimension and color format of the
// specified buffer.
EXPORT void halide_opengl_dev_malloc(void *uctx, buffer_t *buf) {
    halide_opengl_init(uctx);

    if (!buf) {
        halide_error(uctx, "Invalid buffer");
        return;
    }

    // If the texture was already created by the host application, check that
    // it has the correct format. Otherwise, allocate and set up an
    // appropriate texture.
    GLuint tex = get_texture_id(buf);
    bool halide_allocated = false;
    GLint format = 0;
    GLint width, height;
    if (tex != 0) {
        ST.BindTexture(GL_TEXTURE_2D, tex);
        ST.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        ST.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        CHECK_GLERROR();
        if (width < buf->extent[0] || height < buf->extent[1]) {
#ifdef DEBUG
            halide_printf(uctx, "Texture size: %dx%d, buffer size: %dx%d\n",
                          width, height, buf->extent[0], buf->extent[1]);
#endif
            halide_error(uctx, "Existing texture is smaller than buffer");
            return;
        }
    } else {
        if (buf->extent[3] > 1) {
            halide_error(uctx, "3D textures are not supported");
            return;
        }

        // Generate texture ID
        ST.GenTextures(1, &tex);
        CHECK_GLERROR();

        // Set parameters for this texture: no interpolation and clamp to edges.
        ST.BindTexture(GL_TEXTURE_2D, tex);
        ST.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        ST.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        ST.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        ST.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        CHECK_GLERROR();

        // Create empty texture here and fill it with glTexSubImage2D later.
        GLint type = GL_UNSIGNED_BYTE;
        if (!get_texture_format(uctx, buf, &format, &type)) {
            halide_error(uctx, "Invalid texture format\n");
            return;
        }
        width = buf->extent[0];
        height = buf->extent[1];

        ST.TexImage2D(GL_TEXTURE_2D, 0, format,
                      width, height, 0, format, type, NULL);
        CHECK_GLERROR();

        buf->dev = tex;
        halide_allocated = true;
#ifdef DEBUG
        halide_printf(uctx, "Allocated texture of size %dx%d\n", width, height);
#endif

        ST.BindTexture(GL_TEXTURE_2D, 0);
    }

    // Record main information about texture and remember it for later. In
    // halide_opengl_dev_run we are only given the texture ID and not the full
    // buffer_t, so we copy the interesting information here.
    HalideOpenGLTexture *texinfo = (HalideOpenGLTexture*)
        malloc(sizeof(HalideOpenGLTexture));
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

EXPORT HalideOpenGLTexture* halide_opengl_find_texture(GLuint tex) {
    HalideOpenGLTexture* texinfo = ST.textures;
    for (; texinfo; texinfo = texinfo->next)
        if (texinfo->id == tex)
            return texinfo;
    return NULL;
}

// Delete all texture information associated with a buffer. The OpenGL texture
// itself is only deleted if it was actually allocated by Halide and not
// provided by the host application.
EXPORT void halide_opengl_dev_free(void* uctx, buffer_t* buf) {
    CHECK_INITIALIZED();

    GLuint tex = get_texture_id(buf);
    if (tex == 0)
        return;

    // Look up corresponding HalideOpenGLTexture and unlink it from the list.
    HalideOpenGLTexture** ptr = &ST.textures;
    HalideOpenGLTexture* texinfo = *ptr;
    for (; texinfo != NULL; ptr = &texinfo->next, texinfo = *ptr) {
        if (texinfo->id == tex) {
            *ptr = texinfo->next;
            texinfo->next = NULL;
            break;
        }
    }
    if (!texinfo) {
        halide_error(uctx, "Internal error: texture not found");
        return;
    }

    // Delete texture if it was allocated by us.
    if (texinfo->halide_allocated) {
        ST.DeleteTextures(1, &tex);
        CHECK_GLERROR();
        buf->dev = 0;
    }

    free(texinfo);
}

// Called at the beginning of a code block generated by Halide. This function
// is responsible for setting up the OpenGL environment and compiling the GLSL
// code into a fragment shader.
EXPORT void *halide_opengl_init_kernels(void *uctx, void *state_ptr,
                                        const char *src, int size) {
    if (!ST.initialized)
        halide_opengl_init(uctx);

    // Use '/// KERNEL' comments to split 'src' into discrete blocks, one for
    // each kernel contained in it.
    char *begin = strstr(src, kernel_marker);
    char *end = NULL;
    for (; begin && begin[0]; begin = end) {
        end = strstr(begin + sizeof(kernel_marker) - 1, kernel_marker);
        if (!end) {
            end = begin + strlen(begin);
        }
        HalideOpenGLKernel *kernel = create_kernel(uctx, begin, end - begin);
        if (!kernel) {
            // Simply skip invalid kernels
            continue;
        }

#ifdef DEBUG
        halide_printf(uctx, "Defining kernel '%s'\n", kernel->name);
#endif
        // Compile shader
        kernel->shader_id = halide_opengl_make_shader(uctx, GL_FRAGMENT_SHADER,
                                                      kernel->source, NULL);

        // Link GLSL program
        GLuint program = ST.CreateProgram();
        ST.AttachShader(program, ST.vertex_shader_id);
        ST.AttachShader(program, kernel->shader_id);
        ST.LinkProgram(program);
        GLint status;
        ST.GetProgramiv(program, GL_LINK_STATUS, &status);
        if (!status) {
            halide_printf(uctx, "Could not link GLSL program:\n");
            GLint log_len;
            ST.GetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
            char* log = (char*) malloc(log_len);
            ST.GetProgramInfoLog(program, log_len, NULL, log);
            halide_printf(uctx, "%s", log);
            free(log);
            ST.DeleteProgram(program);
            program = 0;
        }
        kernel->program_id = program;

        if (halide_opengl_find_kernel(kernel->name)) {
            halide_printf(uctx, "Duplicate kernel name '%s'\n", kernel->name);
            halide_opengl_delete_kernel(uctx, kernel);
        } else {
            kernel->next = ST.kernels;
            ST.kernels = kernel;
        }
    }
    return NULL;
}

EXPORT void halide_opengl_dev_sync(void* uctx) {
    CHECK_INITIALIZED();
    // TODO: glFinish()
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

// Copy image data from host memory to texture. We assume that the texture has
// already been allocated using halide_opengl_dev_malloc.
EXPORT void halide_opengl_copy_to_dev(void *uctx, buffer_t *buf) {
    CHECK_INITIALIZED();
    if (!buf->host_dirty) return;

    if (!buf->host || !buf->dev) {
#ifdef DEBUG
        print_buffer(uctx, buf);
#endif
        halide_error(uctx, "Invalid copy_to_dev operation");
        return;
    }

    GLuint tex = get_texture_id(buf);
#ifdef DEBUG
    halide_printf(uctx, "halide_copy_to_dev: %d\n", tex);
#endif

    ST.BindTexture(GL_TEXTURE_2D, tex);
    CHECK_GLERROR();

    GLint format, type;
    if (!get_texture_format(uctx, buf, &format, &type)) {
        halide_error(uctx, "Invalid texture format\n");
        return;
    }
    GLint width = buf->extent[0];
    GLint height = buf->extent[1];

    bool is_interleaved =
        (buf->stride[2] == 1 && buf->stride[0] == buf->extent[2]);
    if (is_interleaved) {
        // TODO: GL_UNPACK_ROW_LENGTH
        ST.TexSubImage2D(GL_TEXTURE_2D, 0,
                         0, 0, width, height,
                         format, type, buf->host);
        CHECK_GLERROR();
    } else {
        size_t size = width * height * buf->extent[2] * buf->elem_size;
        void *tmp = halide_malloc(uctx, size);

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
        CHECK_GLERROR();

        halide_free(uctx, tmp);
    }
    ST.BindTexture(GL_TEXTURE_2D, 0);
    buf->host_dirty = false;
}

// Copy image data from texture back to host memory.
EXPORT void halide_opengl_copy_to_host(void *uctx, buffer_t *buf) {
    CHECK_INITIALIZED();
    if (!buf->dev_dirty)
        return;

    if (!buf->host || !buf->dev) {
#ifdef DEBUG
        print_buffer(uctx, buf);
#endif
        halide_error(uctx, "Invalid copy_to_host operation");
        return;
    }

    GLuint tex = get_texture_id(buf);
#ifdef DEBUG
    halide_printf(uctx, "halide_copy_to_host: %d\n", tex);
#endif

    GLint format;
    GLint type;
    if (!get_texture_format(uctx, buf, &format, &type)) {
        halide_error(uctx, "Invalid texture format\n");
        return;
    }
    GLint width = buf->extent[0];
    GLint height = buf->extent[1];

    ST.BindTexture(GL_TEXTURE_2D, tex);
    CHECK_GLERROR();
    bool is_interleaved =
        (buf->stride[2] == 1 && buf->stride[0] == buf->extent[2]);
    if (is_interleaved) {
        // TODO: GL_UNPACK_ROW_LENGTH
        ST.GetTexImage(GL_TEXTURE_2D, 0, format, type, buf->host);
        CHECK_GLERROR();
    } else {
        size_t size = width * height * buf->extent[2] * buf->elem_size;
        uint8_t *tmp = (uint8_t*)halide_malloc(uctx, size);

        ST.PixelStorei(GL_PACK_ALIGNMENT, 1);
        ST.GetTexImage(GL_TEXTURE_2D, 0, format, type, tmp);
        CHECK_GLERROR();

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

        halide_free(uctx, tmp);
    }

    ST.BindTexture(GL_TEXTURE_2D, 0);
    buf->dev_dirty = false;
}


EXPORT void halide_opengl_dev_run(
    void* uctx,
    void* state_ptr,
    const char* entry_name,
    int blocksX, int blocksY, int blocksZ,
    int threadsX, int threadsY, int threadsZ,
    int shared_mem_bytes,
    size_t arg_sizes[],
    void* args[])
{
    CHECK_INITIALIZED();

    HalideOpenGLKernel* kernel = halide_opengl_find_kernel(entry_name);
    if (!kernel) {
        halide_printf(uctx, "Could not find a kernel named '%s'\n",
                      entry_name);
        return;
    }

    ST.UseProgram(kernel->program_id);

    HalideOpenGLArgument* kernel_arg;

    // Copy input arguments to corresponding GLSL uniforms.
    GLint num_active_textures = 0;
    kernel_arg = kernel->arguments;
    for (int i = 0; args[i]; i++, kernel_arg = kernel_arg->next) {
        if (!kernel_arg) {
            halide_printf(uctx, "Argument %d: size=%d value=%p\n", i,
                          arg_sizes[i], args[i]);
            halide_error(uctx,
                         "Too many arguments passed to halide_opengl_dev_run");
            return;
        }

        if (kernel_arg->kind == ARGKIND_OUTBUF) {
            // Outbuf textures are handled explicitly below
            continue;
        } else if (kernel_arg->kind == ARGKIND_INBUF) {
            GLint loc =
                ST.GetUniformLocation(kernel->program_id, kernel_arg->name);
            if (loc == -1) {
                halide_error(uctx, "No sampler defined for input texture.\n");
                return;
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
            if (loc == -1) {
// Argument was probably optimized away by GLSL compiler.
#ifdef DEBUG
                halide_printf(uctx, "Ignoring argument '%s'\n",
                              kernel_arg->name);
#endif
                continue;
            }

            switch (kernel_arg->type) {
            case ARGTYPE_INT:
                halide_printf(uctx, "Int argument %d (%s): %d\n", i,
                              kernel_arg->name, *((int *)args[i]));
                ST.Uniform1iv(loc, 1, (GLint *)args[i]);
                break;
            case ARGTYPE_FLOAT: {
                halide_printf(uctx, "Float argument %d (%s): %g\n", i,
                              kernel_arg->name, *((float *)args[i]));
                ST.Uniform1fv(loc, 1, (GLfloat *)args[i]);
                break;
            }
            case ARGTYPE_NONE:
            default:
                halide_error(uctx, "Unknown kernel argument type");
                return;
            }
        }
    }
    if (kernel_arg) {
        halide_error(uctx, "Too few arguments passed to halide_opengl_dev_run");
        return;
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
            halide_error(uctx,
			 "OpenGL ES 2.0 only supports one single output texture");
	    return;
        }

        GLuint tex = *((GLuint*)args[i]);
#ifdef DEBUG
        halide_printf(uctx, "Output texture %d: %d\n", num_output_textures, tex);
#endif
        ST.FramebufferTexture2D(GL_FRAMEBUFFER,
                                GL_COLOR_ATTACHMENT0 + num_output_textures,
                                GL_TEXTURE_2D, tex, 0);
        CHECK_GLERROR();

        HalideOpenGLTexture* texinfo = halide_opengl_find_texture(tex);
	if (!texinfo) {
	    halide_error(uctx, "Undefined output texture");
	    return;
	}
        output_min[0] = texinfo->min[0];
        output_min[1] = texinfo->min[1];
        output_extent[0] = texinfo->extent[0];
        output_extent[1] = texinfo->extent[1];
        num_output_textures++;
    }
    // TODO: GL_MAX_DRAW_BUFFERS
    if (num_output_textures == 0) {
        halide_printf(uctx, "Warning: kernel '%s' has no output\n",
                      kernel->name);
        // TODO: cleanup
        return;
    } else {
        GLenum *draw_buffers = (GLenum*)
            malloc(num_output_textures * sizeof(GLenum));
        for (int i=0; i<num_output_textures; i++)
            draw_buffers[i] = GL_COLOR_ATTACHMENT0 + i;
        ST.DrawBuffers(num_output_textures, draw_buffers);
        CHECK_GLERROR();
        free(draw_buffers);
    }

    // Check that framebuffer is set up correctly
    GLenum status = ST.CheckFramebufferStatus(GL_FRAMEBUFFER);
    CHECK_GLERROR();
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        halide_printf(uctx, "Setting up GL framebuffer %d failed (%x)\n",
                      ST.framebuffer_id, status);
        // TODO: cleanup
        return;
    }

    // Set vertex attributes
    GLint loc = ST.GetUniformLocation(kernel->program_id, "output_extent");
    ST.Uniform2iv(loc, 1, output_extent);
    CHECK_GLERROR();
    loc = ST.GetUniformLocation(kernel->program_id, "output_min");
    ST.Uniform2iv(loc, 1, output_min);
    CHECK_GLERROR();

    // Setup viewport
    ST.Viewport(0, 0, output_extent[0], output_extent[1]);


    // Execute shader
    GLint position = ST.GetAttribLocation(kernel->program_id,
                                          "position");
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
    CHECK_GLERROR();
    ST.DisableVertexAttribArray(position);

    // Cleanup
    for (int i = 0; i < num_active_textures; i++) {
        ST.ActiveTexture(GL_TEXTURE0 + i);
        ST.BindTexture(GL_TEXTURE_2D, 0);
    }
    ST.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

//  Create wrappers that satisfy old naming conventions

EXPORT void halide_release(void *uctx) { halide_opengl_release(uctx); }

EXPORT void halide_dev_malloc(void *uctx, buffer_t *buf) {
    halide_opengl_dev_malloc(uctx, buf);
}

EXPORT void halide_dev_free(void *uctx, buffer_t *buf) {
    halide_opengl_dev_free(uctx, buf);
}

EXPORT void halide_copy_to_host(void *uctx, buffer_t *buf) {
    halide_opengl_copy_to_host(uctx, buf);
}

EXPORT void halide_copy_to_dev(void *uctx, buffer_t *buf) {
    halide_opengl_copy_to_dev(uctx, buf);
}

EXPORT void halide_dev_run(
    void *user_context,
    void *state_ptr,
    const char* entry_name,
    int blocksX, int blocksY, int blocksZ,
    int threadsX, int threadsY, int threadsZ,
    int shared_mem_bytes,
    size_t arg_sizes[],
    void* args[]) {
    halide_opengl_dev_run(user_context, state_ptr,
                          entry_name,
                          blocksX, blocksY, blocksZ,
                          threadsX, threadsY, threadsY,
                          shared_mem_bytes,
                          arg_sizes,
                          args);
}

EXPORT void halide_dev_sync(void *user_context) {
    halide_opengl_dev_sync(user_context);
}

EXPORT void *halide_init_kernels(void *user_context, void *state_ptr,
                                 const char *src, int size) {
    return halide_opengl_init_kernels(user_context, state_ptr, src, size);
}
