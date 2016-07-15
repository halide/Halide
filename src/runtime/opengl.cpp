#include "HalideRuntimeOpenGL.h"
#include "device_interface.h"
#include "printer.h"
#include "mini_opengl.h"

// This constant is used to indicate that the application will take
// responsibility for binding the output render target before calling the
// Halide function.
#define HALIDE_OPENGL_RENDER_TARGET ((uint64_t)-1)

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
    GLFUNC(PFNGLDISABLEPROC, Enable);                                   \
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
    GLFUNC(PFNGLGETVERTEXATTRIBIVPROC, GetVertexAttribiv);              \
    GLFUNC(PFNGLPIXELSTOREIPROC, PixelStorei);                          \
    GLFUNC(PFNGLREADPIXELS, ReadPixels);                                \
    GLFUNC(PFNGLGETSTRINGPROC, GetString);                              \
    GLFUNC(PFNGLGETINTEGERV, GetIntegerv);                              \
    GLFUNC(PFNGLGETBOOLEANV, GetBooleanv);                              \
    GLFUNC(PFNGLFINISHPROC, Finish);

// List of all OpenGL functions used by the runtime, which may not
// exist due to an older or less capable version of GL. In using any
// of these functions, code must test if they are NULL.
#define OPTIONAL_GL_FUNCTIONS                                           \
    GLFUNC(PFNGLGENVERTEXARRAYS, GenVertexArrays);                      \
    GLFUNC(PFNGLBINDVERTEXARRAY, BindVertexArray);                      \
    GLFUNC(PFNGLDELETEVERTEXARRAYS, DeleteVertexArrays);                \
    GLFUNC(PFNDRAWBUFFERS, DrawBuffers)

// ---------- Types ----------

using namespace Halide::Runtime::Internal;

namespace Halide { namespace Runtime { namespace Internal { namespace OpenGL {

extern WEAK halide_device_interface opengl_device_interface;

WEAK const char *gl_error_name(int32_t err) {
  const char *result;
  switch (err) {
  case 0x500:
    result = "GL_INVALID_ENUM";
    break;
  case 0x501:
    result = "GL_INVALID_VALUE";
    break;
  case 0x502:
    result = "GL_INVALID_OPERATION";
    break;
  case 0x503:
    result = "GL_STACK_OVERFLOW";
    break;
  case 0x504:
    result = "GL_STACK_UNDERFLOW";
    break;
  case 0x505:
    result = "GL_OUT_OF_MEMORY";
    break;
  case 0x506:
    result = "GL_INVALID_FRAMEBUFFER_OPERATION";
    break;
  case 0x507:
    result = "GL_CONTEXT_LOST";
    break;
  case 0x8031:
    result = "GL_TABLE_TOO_LARGE";
    break;
  default:
    result = "<unknown GL error>";
    break;
  }
  return result;
}

struct HalideMalloc {
     __attribute__((always_inline)) HalideMalloc(void *user_context, size_t size)
        : user_context(user_context), ptr(halide_malloc(user_context, size)) {}
     __attribute__((always_inline)) ~HalideMalloc() {
        halide_free(user_context, ptr);
    }
    void * const user_context;
    void * const ptr;
};

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
    bool CheckAndReportError(void *user_context, const char *location);

    bool initialized;

    // Information about the OpenGL platform we're running on.
    OpenGLProfile profile;
    int major_version, minor_version;
    bool have_vertex_array_objects;
    bool have_texture_rg;
    bool have_texture_float;
    bool have_texture_rgb8_rgba8;

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
    OPTIONAL_GL_FUNCTIONS;
#undef GLFUNC
};

WEAK bool GlobalState::CheckAndReportError(void *user_context, const char *location) {
    GLenum err = GetError();
    if (err != GL_NO_ERROR) {
        error(user_context) << "OpenGL error " << gl_error_name(err) << "(" << (int)err << ")" <<
            " at " << location << ".\n" ;
        return true;
    }
    return false;
}

WEAK GlobalState global_state;

// Saves & restores OpenGL state
class GLStateSaver {
    public:

    GLStateSaver() { save(); }
    ~GLStateSaver() { restore(); }

    private:

    // The state variables
    GLint active_texture;
    GLint array_buffer_binding;
    GLint element_array_buffer_binding;
    GLint framebuffer_binding;
    GLint program;
    GLint vertex_array_binding;
    GLint viewport[4];
    GLboolean cull_face;
    GLboolean depth_test;
    int max_combined_texture_image_units;
    GLint *texture_2d_binding;
    int max_vertex_attribs;
    GLint *vertex_attrib_array_enabled;

    // Define these out-of-line as WEAK, to avoid LLVM error "MachO doesn't support COMDATs"
    void save();
    void restore();
};

WEAK void GLStateSaver::save()
{
    global_state.GetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    global_state.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer_binding);
    global_state.GetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &element_array_buffer_binding);
    global_state.GetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_binding);
    global_state.GetIntegerv(GL_CURRENT_PROGRAM, &program);
    global_state.GetBooleanv(GL_CULL_FACE, &cull_face);
    global_state.GetBooleanv(GL_DEPTH_TEST, &depth_test);
    global_state.GetIntegerv(GL_VIEWPORT, viewport);

    global_state.GetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &max_combined_texture_image_units);
    texture_2d_binding = (GLint *) malloc(max_combined_texture_image_units * sizeof(GLint));
    for (int i=0; i < max_combined_texture_image_units; i++) {
        global_state.ActiveTexture(GL_TEXTURE0 + i);
        global_state.GetIntegerv(GL_TEXTURE_BINDING_2D, &texture_2d_binding[i]);
    }

    global_state.GetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_vertex_attribs);
    vertex_attrib_array_enabled = (GLint *) malloc(max_vertex_attribs * sizeof(GLint));
    for (int i=0; i< max_vertex_attribs; i++) {
        global_state.GetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &vertex_attrib_array_enabled[i]);
    }

    if (global_state.have_vertex_array_objects) {
        global_state.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertex_array_binding);
    }

#ifdef DEBUG_RUNTIME
    debug(NULL) << "Saved OpenGL state\n";
#endif
}

WEAK void GLStateSaver::restore()
{
#ifdef DEBUG_RUNTIME
    debug(NULL) << "Restoring OpenGL state\n";
#endif

    for (int i=0; i < max_combined_texture_image_units; i++) {
        global_state.ActiveTexture(GL_TEXTURE0 + i);
        global_state.BindTexture(GL_TEXTURE_2D, texture_2d_binding[i]);
    }
    free(texture_2d_binding);

    for (int i=0; i< max_vertex_attribs; i++) {
        if (vertex_attrib_array_enabled[i])
            global_state.EnableVertexAttribArray(i);
        else
            global_state.DisableVertexAttribArray(i);
    }
    free(vertex_attrib_array_enabled);

    if (global_state.have_vertex_array_objects) {
        global_state.BindVertexArray(vertex_array_binding);
    }

    global_state.ActiveTexture(active_texture);
    global_state.BindFramebuffer(GL_FRAMEBUFFER, framebuffer_binding);
    global_state.BindBuffer(GL_ARRAY_BUFFER, array_buffer_binding);
    global_state.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_array_buffer_binding);
    global_state.UseProgram(program);
    global_state.Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    (cull_face ? global_state.Enable : global_state.Disable)(GL_CULL_FACE);
    (depth_test ? global_state.Enable : global_state.Disable)(GL_DEPTH_TEST);
}

// A list of module-specific state. Each module corresponds to a single Halide filter
WEAK ModuleState *state_list;

WEAK const char *kernel_marker = "/// KERNEL ";
WEAK const char *input_marker  = "/// IN_BUFFER ";
WEAK const char *output_marker = "/// OUT_BUFFER ";
WEAK const char *uniform_marker    = "/// UNIFORM ";
WEAK const char *varying_marker    = "/// VARYING ";

// ---------- Helper functions ----------

WEAK char *strndup(const char *s, size_t n) {
    char *p = (char*)malloc(n+1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

WEAK void debug_buffer(void *user_context, buffer_t *buf) {
    debug(user_context)
        << "  dev: " << buf->dev << "\n"
        << "  texture_id: " << (GLuint)halide_get_device_handle(buf->dev) << "\n"
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
#ifdef DEBUG_RUNTIME
{
    debug(user_context) << ((type == GL_VERTEX_SHADER) ? "GL_VERTEX_SHADER" : "GL_FRAGMENT_SHADER")
                        << " SOURCE:\n";
    // debug() will go thru Printer<> which has a fixed, non-growing size.
    // Just pass the source directly to halide_print instead, so it won't get clipped.
    halide_print(user_context, source);
}
#endif

    GLuint shader = global_state.CreateShader(type);
    if (global_state.CheckAndReportError(user_context, "make_shader(1)")) {
        return 1;
    }
    if (*source == '\0') {
        debug(user_context) << "Halide GLSL: passed shader source is empty, using default.\n";
        const char *default_shader = "varying vec2 pixcoord;\n void main() { }";
        global_state.ShaderSource(shader, 1, (const GLchar **)&default_shader, NULL);
    } else {
        global_state.ShaderSource(shader, 1, (const GLchar **)&source, length);
    }
    if (global_state.CheckAndReportError(user_context, "make_shader(2)")) {
        return 1;
    }
    global_state.CompileShader(shader);
    if (global_state.CheckAndReportError(user_context, "make_shader(3)")) {
        return 1;
    }

    GLint shader_ok = 0;
    global_state.GetShaderiv(shader, GL_COMPILE_STATUS, &shader_ok);
    if (!shader_ok) {
        print(user_context) << "Could not compile shader:\n";
        GLint log_len;
        global_state.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        HalideMalloc log_tmp(user_context, log_len);
        if (log_tmp.ptr) {
            char *log = (char*) log_tmp.ptr;
            global_state.GetShaderInfoLog(shader, log_len, NULL, log);
            print(user_context) << log << "\n";
        }
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
    kernel->program_id = 0;

    debug(user_context) << "Compiling GLSL kernel (size = " << size << "):\n";

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
    global_state.DeleteProgram(kernel->program_id);
#if 0 // TODO figure out why this got deleted.
    global_state.DeleteShader(kernel->shader_id);
#endif

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
    framebuffer_id = 0;
    vertex_array_object = vertex_buffer = element_buffer = 0;
    textures = NULL;
    have_vertex_array_objects = false;
    have_texture_rg = false;
    have_texture_rgb8_rgba8 = false;
    // Initialize all GL function pointers to NULL
#define GLFUNC(type, name) name = NULL;
    USED_GL_FUNCTIONS;
    OPTIONAL_GL_FUNCTIONS;
#undef GLFUNC
}

WEAK int load_gl_func(void *user_context, const char *name, void **ptr, bool required) {
    void *p = halide_opengl_get_proc_address(user_context, name);
    if (!p && required) {
        error(user_context) << "Could not load function pointer for " << name;
        return -1;
    }
    *ptr = p;
    return 0;
}

WEAK bool extension_supported(void *user_context, const char *name) {
    // Iterate over space delimited extension strings. Note that glGetStringi
    // is not part of GL ES 2.0, and not reliable in all implementations of
    // GL ES 3.0.
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

    return false;
}

// Check for availability of various version- and extension-specific features
// and hook up functions pointers as necessary
WEAK void init_extensions(void *user_context) {
    if (global_state.major_version >= 3) { // This is likely valid for both OpenGL and OpenGL ES
        load_gl_func(user_context, "glGenVertexArrays", (void**)&global_state.GenVertexArrays, false);
        load_gl_func(user_context, "glBindVertexArray", (void**)&global_state.BindVertexArray, false);
        load_gl_func(user_context, "glDeleteVertexArrays", (void**)&global_state.DeleteVertexArrays, false);
        if (global_state.GenVertexArrays && global_state.BindVertexArray && global_state.DeleteVertexArrays) {
            global_state.have_vertex_array_objects = true;
        }
    }
    load_gl_func(user_context, "glDrawBuffers", (void**)&global_state.DrawBuffers, false);

    global_state.have_texture_rg =
        global_state.major_version >= 3 ||
        (global_state.profile == OpenGL &&
         extension_supported(user_context, "GL_ARB_texture_rg")) ||
        (global_state.profile == OpenGLES &&
         extension_supported(user_context, "GL_EXT_texture_rg"));

    global_state.have_texture_rgb8_rgba8 =
        global_state.major_version >= 3 ||
        (global_state.profile == OpenGLES &&
         extension_supported(user_context, "GL_OES_rgb8_rgba8"));

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

#ifdef DEBUG_RUNTIME
    halide_start_clock(user_context);
#endif

    global_state.init();

    // Make a context if there isn't one
    if (halide_opengl_create_context(user_context)) {
        error(user_context) << "Failed to make OpenGL context";
        return -1;
    }

    // Initialize pointers to core OpenGL functions.
#define GLFUNC(TYPE, VAR)                                               \
    if (load_gl_func(user_context, "gl" #VAR, (void**)&global_state.VAR, true) < 0) { \
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
        << "Halide running on OpenGL " << ((global_state.profile == OpenGL) ? "" : "ES ") << major << "." << minor << "\n"
        << "  vertex_array_objects: " << (global_state.have_vertex_array_objects ? "yes\n" : "no\n")
        << "  texture_rg: " << (global_state.have_texture_rg ? "yes\n" : "no\n")
        << "  have_texture_rgb8_rgba8: " << (global_state.have_texture_rgb8_rgba8 ? "yes\n" : "no\n")
        << "  texture_float: " << (global_state.have_texture_float ? "yes\n" : "no\n");

    // Initialize framebuffer.
    global_state.GenFramebuffers(1, &global_state.framebuffer_id);
    if (global_state.CheckAndReportError(user_context, "halide_opengl_init GenFramebuffers")) {
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
        if (global_state.CheckAndReportError(user_context, "halide_opengl_init GenVertexArrays")) {
            return 1;
        }
    }

    global_state.initialized = true;
    return 0;
}

// Release all data allocated by the runtime.
//
// The OpenGL context itself is generally managed by the host application, so
// we leave it untouched.
WEAK int halide_opengl_device_release(void *user_context) {
    if (!global_state.initialized) {
        return 0;
    }

    debug(user_context) << "halide_opengl_release\n";
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
            debug(user_context) << "halide_opengl_device_release: Deleting texture " << tex->id << "\n";
            global_state.DeleteTextures(1, &tex->id);
            if (global_state.CheckAndReportError(user_context, "halide_opengl_device_release DeleteTextures")) {
                return 1;
            }
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

    // GL_LUMINANCE and GL_LUMINANCE_ALPHA aren't color-renderable in ES2, period,
    // thus can't be read back via ReadPixels, thus are nearly useless to us.
    // GL_RED and GL_RG are technically optional in ES2 (required in ES3),
    // but as a practical matter, they are supported on pretty much every recent device
    // (iOS: everything >= iPhone 4s; Android: everything >= 4.3 plus various older devices).
    // This is definitely suboptimal; the only real alternative would be to implement
    // these as GL_RGB or GL_RGBA, ignoring the extra channels.
    if (channels <= 2 && !global_state.have_texture_rg) {
        error(user_context) << "OpenGL: 1 and 2 channel textures are not supported for this version of OpenGL.";
        return false;
    }

    // Common formats supported by both GLES 2.0 and GL 2.1 are selected below
    //
    switch(channels) {
    case 0:
    case 1:
        *format = GL_RED;
        break;
    case 2:
        *format = GL_RG;
        break;
    case 3:
        *format = GL_RGB;
        break;
    case 4:
        *format = GL_RGBA;
        break;
    default:
        error(user_context) << "OpenGL: Invalid number of color channels: " << channels;
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
            case GL_RED:
            case GL_RG:
            case GL_RGB:
            case GL_RGBA:
                *internal_format = GL_RGBA32F;
                break;
            default:
                error(user_context) << "OpenGL: Cannot select internal format for format " << *format;
                return false;
            }
        } else {
            *internal_format = *format;
        }
        break;
    }

    return true;
}

// This function returns the width, height and number of color channels that the
// texture for the specified buffer_t will contain. It provides a single place
// to implement the logic snapping zero sized dimensions to one element.
WEAK bool get_texture_dimensions(void *user_context, buffer_t *buf, GLint *width,
                            GLint *height, GLint *channels) {

    *width = buf->extent[0];
    if (*width == 0) {
        error(user_context) << "Invalid extent[0]: " << *width << "\n";
        return false;
    }

    // GLES 2.0 supports GL_TEXTURE_2D (plus cube map), but not 1d or 3d. If we
    // end up with a buffer that has a zero extent, set the corresponding size
    // to one.
    *height = (buf->extent[1]) ? buf->extent[1] : 1;
    *channels = (buf->extent[2]) ? buf->extent[2] : 1;

    return true;
}

WEAK TextureInfo *find_texture_info(GLuint tex) {
    TextureInfo *texinfo = global_state.textures;
    while (texinfo && texinfo->id != tex) {
        texinfo = texinfo->next;
    }
    return texinfo;
}

// Allocate new TextureInfo, fill it in, add to global list.
WEAK TextureInfo *new_texture_info(GLuint tex, int32_t min[4], int32_t extent[4], bool halide_allocated) {
    TextureInfo *texinfo = (TextureInfo*)malloc(sizeof(TextureInfo));
    texinfo->id = tex;
    for (int i=0; i<3; i++) {
        texinfo->min[i] = min[i];
        texinfo->extent[i] = extent[i];
    }
    texinfo->halide_allocated = halide_allocated;

    texinfo->next = global_state.textures;
    global_state.textures = texinfo;

   return texinfo;
}

// Find TextureInfo with the given texture ID and remove it from the global list,
// but DO NOT free the TextureInfo itself. If not found, return NULL.
WEAK TextureInfo *unlink_texture_info(GLuint tex) {
    TextureInfo **ptr = &global_state.textures;
    TextureInfo *texinfo = *ptr;
    for (; texinfo != NULL; ptr = &texinfo->next, texinfo = *ptr) {
        if (texinfo->id == tex) {
            *ptr = texinfo->next;
            texinfo->next = NULL;
            return texinfo;
        }
    }
    return NULL;
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
    GLuint tex = 0;
    bool halide_allocated = false;

    if (buf->dev) {
#ifdef HAVE_GLES3
        // Look up the width and the height from the existing texture. Note that
        // glGetTexLevelParameteriv does not support GL_TEXTURE_WIDTH or
        // GL_TEXTURE_HEIGHT in GLES 2.0
        GLint width, height;
        global_state.BindTexture(GL_TEXTURE_2D, tex);
        global_state.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        global_state.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        if (global_state.CheckAndReportError(user_context, "halide_opengl_device_malloc binding texture (GLES3)")) {
            return 1;
        }
        if (width < buf->extent[0] || height < buf->extent[1]) {
            error(user_context)
                << "Existing texture is smaller than buffer. "
                << "Texture size: " << width << "x" << height
                << ", buffer size: " << buf->extent[0] << "x" << buf->extent[1];
            return 1;
        }
#endif
        uint64_t handle = halide_get_device_handle(buf->dev);
        tex = (handle == HALIDE_OPENGL_RENDER_TARGET) ? 0 : (GLuint)handle;
    } else {
        if (buf->extent[3] > 1) {
            error(user_context) << "3D textures are not supported";
            return 1;
        }

        // Generate texture ID
        global_state.GenTextures(1, &tex);
        if (global_state.CheckAndReportError(user_context, "halide_opengl_device_malloc GenTextures")) {
            global_state.DeleteTextures(1, &tex);
            return 1;
        }

        // Set parameters for this texture: no interpolation and clamp to edges.
        global_state.BindTexture(GL_TEXTURE_2D, tex);
        global_state.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        global_state.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        global_state.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        global_state.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (global_state.CheckAndReportError(user_context, "halide_opengl_device_malloc binding texture")) {
            global_state.DeleteTextures(1, &tex);
            return 1;
        }

        // Create empty texture here and fill it with glTexSubImage2D later.
        GLint internal_format, format, type;
        if (!get_texture_format(user_context, buf, &internal_format, &format, &type)) {
            error(user_context) << "Invalid texture format";
            global_state.DeleteTextures(1, &tex);
            return 1;
        }

        GLint width, height, channels;
        if (!get_texture_dimensions(user_context, buf, &width, &height, &channels)) {
            error(user_context) << "Invalid texture dimensions";
            return 1;
        }

        global_state.TexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, type, NULL);
        if (global_state.CheckAndReportError(user_context, "halide_opengl_device_malloc TexImage2D")) {
            global_state.DeleteTextures(1, &tex);
            return 1;
        }

        buf->dev = halide_new_device_wrapper(tex, &opengl_device_interface);
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
    if (!find_texture_info(tex)) {
        (void) new_texture_info(tex, buf->min, buf->extent, halide_allocated);
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

    if (buf->dev == 0) {
        return 0;
    }

    uint64_t handle = halide_get_device_handle(buf->dev);
    GLuint tex = (handle == HALIDE_OPENGL_RENDER_TARGET) ? 0 : (GLuint)handle;

    // Look up corresponding TextureInfo and unlink it from the list.
    TextureInfo *texinfo = unlink_texture_info(tex);
    if (!texinfo) {
        error(user_context) << "Internal error: texture " << tex << " not found.";
        return 1;
    }

    // Delete texture if it was allocated by us.
    int result = 0;
    if (texinfo->halide_allocated) {
        debug(user_context) << "halide_opengl_device_free: Deleting texture " << tex << "\n";
        global_state.DeleteTextures(1, &tex);
        if (global_state.CheckAndReportError(user_context, "halide_opengl_device_free DeleteTextures")) {
            result = 1;
            // do not return: we want to delete the wrapper and zero the dev field,
            // even if we can't delete the texture.
        }
        halide_delete_device_wrapper(buf->dev);
        buf->dev = 0;
    }

    free(texinfo);

    return result;
}

// Can't use std::min, std::max in Halide runtime.
template<typename T> __attribute__((always_inline)) T std_min(T a, T b) { return (a < b) ? a : b; }
template<typename T> __attribute__((always_inline)) T std_max(T a, T b) { return (a > b) ? a : b; }

// This method copies image data from the layout specified by the strides of the
// buffer_t to the packed interleaved format needed by GL. It is assumed that
// src and dst have the same number of channels.
template <class T>
 __attribute__((always_inline))
 void halide_to_interleaved(const buffer_t *src_buf, T *dst) {
    const T *src = reinterpret_cast<const T *>(src_buf->host);
    int width = std_max<int>(1, src_buf->extent[0]);
    int height = std_max<int>(1, src_buf->extent[1]);
    int channels = std_max<int>(1, src_buf->extent[2]);
    int x_stride = src_buf->stride[0];
    int y_stride = src_buf->stride[1];
    int c_stride = src_buf->stride[2];
    for (int y = 0; y < height; y++) {
        int dstidx = y * width * channels;
        for (int x = 0; x < width; x++) {
            int srcidx = y * y_stride + x * x_stride;
            for (int c = 0; c < channels; c++) {
                dst[dstidx] = src[srcidx];
                srcidx += c_stride;
                dstidx += 1;
            }
        }
    }
}

// This method copies image data from the packed interleaved format needed by GL
// to the arbitrary strided layout specified by the buffer_t. If src has fewer
// channels than dst, the excess in dst will be left untouched; if src has
// more channels than dst, the excess will be ignored.
template <class T>
__attribute__((always_inline))
void interleaved_to_halide(void *user_context, const T *src, int src_channels, buffer_t *dst_buf) {
    T *dst = reinterpret_cast<T*>(dst_buf->host);
    int width = std_max<int>(1, dst_buf->extent[0]);
    int height = std_max<int>(1, dst_buf->extent[1]);
    int dst_channels = std_max<int>(1, dst_buf->extent[2]);
    int channels = std_min<int>(src_channels, dst_channels);
    int x_stride = dst_buf->stride[0];
    int y_stride = dst_buf->stride[1];
    int c_stride = dst_buf->stride[2];
    int src_skip = std_max(0, src_channels - dst_channels);
    for (int y = 0; y < height; y++) {
        int srcidx = y * width * src_channels;
        for (int x = 0; x < width; x++) {
            int dstidx = y * y_stride + x * x_stride;
            for (int c = 0; c < channels; c++) {
                dst[dstidx] = src[srcidx];
                srcidx += 1;
                dstidx += c_stride;
            }
            srcidx += src_skip;
        }
    }
}

// Copy image data from host memory to texture.
WEAK int halide_opengl_copy_to_device(void *user_context, buffer_t *buf) {
    if (!global_state.initialized) {
        error(user_context) << "OpenGL runtime not initialized (halide_opengl_copy_to_device).";
        return 1;
    }

    GLStateSaver state_saver;

    int err = halide_opengl_device_malloc(user_context, buf);
    if (err) {
        return err;
    }

    if (!buf->host || !buf->dev) {
        debug_buffer(user_context, buf);
        error(user_context) << "Invalid copy_to_device operation: host or dev NULL";
        return 1;
    }

    uint64_t handle = halide_get_device_handle(buf->dev);
    if (handle == HALIDE_OPENGL_RENDER_TARGET) {
        // TODO: this isn't correct; we want to ensure we copy to the current render_target.
        debug(user_context) << "halide_opengl_copy_to_device: called for HALIDE_OPENGL_RENDER_TARGET\n";
        return 0;
    }
    GLuint tex = (GLuint)handle;
    debug(user_context) << "halide_opengl_copy_to_device: " << tex << "\n";

    global_state.BindTexture(GL_TEXTURE_2D, tex);
    if (global_state.CheckAndReportError(user_context, "halide_opengl_copy_to_device BindTexture")) {
        return 1;
    }
    GLint internal_format, format, type;
    if (!get_texture_format(user_context, buf, &internal_format, &format, &type)) {
        error(user_context) << "Invalid texture format";
        return 1;
    }

    GLint width, height, buffer_channels;
    if (!get_texture_dimensions(user_context, buf, &width, &height, &buffer_channels)) {
        error(user_context) << "Invalid texture dimensions";
        return 1;
    }

    // To use TexSubImage2D directly, the colors must be stored interleaved
    // and rows must be stored consecutively.
    // (Single-channel buffers are "interleaved" for our purposes here.)
    bool is_interleaved = (buffer_channels == 1) || (buf->stride[2] == 1 && buf->stride[0] == buf->extent[2]);
    bool is_packed = (buf->stride[1] == buf->extent[0] * buf->stride[0]);
    if (is_interleaved && is_packed) {
        global_state.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        uint8_t *host_ptr = buf->host;
        global_state.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, format, type, host_ptr);
        if (global_state.CheckAndReportError(user_context, "halide_opengl_copy_to_device TexSubImage2D(1)")) {
            return 1;
        }
    } else {
        debug(user_context)
            << "Warning: In copy_to_device, host buffer is not interleaved. Doing slow interleave.\n";

        size_t texture_size = width * height * buffer_channels * buf->elem_size;
        HalideMalloc tmp(user_context, texture_size);
        if (!tmp.ptr) {
            error(user_context) << "halide_malloc failed inside copy_to_device";
            return -1;
        }

        switch (type) {
        case GL_UNSIGNED_BYTE:
            halide_to_interleaved<uint8_t>(buf, (uint8_t*)tmp.ptr);
            break;
        case GL_UNSIGNED_SHORT:
            halide_to_interleaved<uint16_t>(buf, (uint16_t*)tmp.ptr);
            break;
        case GL_FLOAT:
            halide_to_interleaved<float>(buf, (float*)tmp.ptr);
            break;
        }

        global_state.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        global_state.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, format, type, tmp.ptr);
        if (global_state.CheckAndReportError(user_context, "halide_opengl_copy_to_device TexSubImage2D(2)")) {
            return 1;
        }
    }

    return 0;
}

// Copy image data from texture back to host memory.
WEAK int halide_opengl_copy_to_host(void *user_context, buffer_t *buf) {
    if (!global_state.initialized) {
        error(user_context) << "OpenGL runtime not initialized (halide_opengl_copy_to_host).";
        return 1;
    }

    GLStateSaver state_saver;

    if (!buf->host || !buf->dev) {
        debug_buffer(user_context, buf);
        error(user_context) << "Invalid copy_to_host operation: host or dev NULL";
        return 1;
    }

    GLint internal_format, format, type;
    if (!get_texture_format(user_context, buf, &internal_format, &format, &type)) {
        error(user_context) << "Invalid texture format";
        return 1;
    }

    GLint width, height, buffer_channels;
    if (!get_texture_dimensions(user_context, buf, &width, &height, &buffer_channels)) {
        error(user_context) << "Invalid texture dimensions";
        return 1;
    }
    GLint texture_channels = buffer_channels;


    uint64_t handle = halide_get_device_handle(buf->dev);
    if (handle != HALIDE_OPENGL_RENDER_TARGET) {
        GLuint tex = (GLuint)handle;
        debug(user_context) << "halide_copy_to_host: texture " << tex << "\n";
        global_state.BindFramebuffer(GL_FRAMEBUFFER, global_state.framebuffer_id);
        if (global_state.CheckAndReportError(user_context, "copy_to_host BindFramebuffer")) {
            return 1;
        }
        global_state.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        if (global_state.CheckAndReportError(user_context, "copy_to_host FramebufferTexture2D")) {
            return 1;
        }
    } else {
        debug(user_context) << "halide_copy_to_host: HALIDE_OPENGL_RENDER_TARGET\n";
    }

    // Check that framebuffer is set up correctly
    GLenum status = global_state.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        error(user_context)
            << "Setting up GL framebuffer " << global_state.framebuffer_id << " failed " << status;
        return 1;
    }

    // The only format/type pairs guaranteed to be readable in GLES2 are GL_RGBA+GL_UNSIGNED_BYTE,
    // plus one other implementation-dependent pair specified here. Spoiler alert:
    // some ES2 implementations return that very same pair here (i.e., they don't support
    // any other formats); in that case, we need to read as RGBA and manually convert to
    // what we need (usually GL_RGB).
    // NOTE: this requires the currently-bound Framebuffer is correct.
    // TODO: short and float will require even more effort on top of this.
    if (global_state.profile == OpenGLES && format == GL_RGB) {
        GLint extra_format, extra_type;
        global_state.GetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &extra_type);
        if (type != GL_UNSIGNED_BYTE && type != extra_type) {
            error(user_context) << "ReadPixels does not support our type; we don't handle this yet.\n";
            return 1;
        }
        global_state.GetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &extra_format);
        if (format != GL_RGBA && format != extra_format) {
            debug(user_context) << "ReadPixels does not support our format; falling back to GL_RGBA\n";
            format = GL_RGBA;
            texture_channels = 4;
        }
    }

    // To download the texture directly, the colors must be stored interleaved
    // and rows must be stored consecutively.
    // (Single-channel buffers are "interleaved" for our purposes here.)
    bool is_interleaved = (buffer_channels == 1) || (buf->stride[2] == 1 && buf->stride[0] == buf->extent[2]);
    bool is_packed = (buf->stride[1] == buf->extent[0] * buf->stride[0]);
    if (is_interleaved && is_packed && texture_channels == buffer_channels) {
        global_state.PixelStorei(GL_PACK_ALIGNMENT, 1);
        uint8_t *host_ptr = buf->host;
#ifdef DEBUG_RUNTIME
        int64_t t1 = halide_current_time_ns(user_context);
#endif
        global_state.ReadPixels(0, 0, buf->extent[0], buf->extent[1], format, type, host_ptr);
#ifdef DEBUG_RUNTIME
        int64_t t2 = halide_current_time_ns(user_context);
#endif
        if (global_state.CheckAndReportError(user_context, "copy_to_host ReadPixels (1)")) {
            return 1;
        }
#ifdef DEBUG_RUNTIME
        debug(user_context)<<"ReadPixels(1) time: "<<(t2-t1)/1e3<<"usec\n";
#endif
    } else {
        debug(user_context)
            << "Warning: In copy_to_host, host buffer is not interleaved, or not a native format. Doing slow deinterleave.\n";

        size_t texture_size = width * height * texture_channels * buf->elem_size;
        HalideMalloc tmp(user_context, texture_size);
        if (!tmp.ptr) {
            error(user_context) << "halide_malloc failed inside copy_to_host";
            return -1;
        }

        global_state.PixelStorei(GL_PACK_ALIGNMENT, 1);
#ifdef DEBUG_RUNTIME
        int64_t t1 = halide_current_time_ns(user_context);
#endif
        global_state.ReadPixels(0, 0, buf->extent[0], buf->extent[1], format, type, tmp.ptr);
#ifdef DEBUG_RUNTIME
        int64_t t2 = halide_current_time_ns(user_context);
        debug(user_context)<<"ReadPixels(2) time: "<<(t2-t1)/1e3<<"usec\n";
#endif
        if (global_state.CheckAndReportError(user_context, "copy_to_host ReadPixels (2)")) {
            return 1;
        }

        // Premature optimization warning: interleaved_to_halide() could definitely
        // be optimized, but ReadPixels() typically takes ~2-10x as long (especially on
        // mobile devices), so the returns will be modest.
#ifdef DEBUG_RUNTIME
        int64_t t3 = halide_current_time_ns(user_context);
#endif
        switch (type) {
        case GL_UNSIGNED_BYTE:
            interleaved_to_halide<uint8_t>(user_context, (uint8_t*)tmp.ptr, texture_channels, buf);
            break;
        case GL_UNSIGNED_SHORT:
            interleaved_to_halide<uint16_t>(user_context, (uint16_t*)tmp.ptr, texture_channels, buf);
            break;
        case GL_FLOAT:
            interleaved_to_halide<float>(user_context, (float*)tmp.ptr, texture_channels, buf);
            break;
        }
#ifdef DEBUG_RUNTIME
        int64_t t4 = halide_current_time_ns(user_context);
        debug(user_context)<<"deinterleave time: "<<(t4-t3)/1e3<<"usec\n";
#endif
    }

    return 0;
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
                           size_t arg_sizes[], void *args[], int8_t is_buffer[],
                           int num_padded_attributes,
                           float* vertex_buffer,
                           int num_coords_dim0,
                           int num_coords_dim1) {
    if (!global_state.initialized) {
        error(user_context) << "OpenGL runtime not initialized (halide_opengl_run).";
        return 1;
    }

    GLStateSaver state_saver;

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
    if (global_state.CheckAndReportError(user_context, "halide_opengl_run UseProgram")) {
        return 1;
    }

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
                << "Too many arguments passed to halide_opengl_run\n"
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
            halide_assert(user_context, is_buffer[i] && "OpenGL Outbuf argument is not a buffer.")
            // Check if the output buffer will be bound by the client instead of
            // the Halide runtime
            uint64_t dev = *(uint64_t *)args[i];
            if (!dev) {
                error(user_context) << "GLSL: Encountered invalid NULL dev pointer";
                return 1;
            }
            uint64_t handle = halide_get_device_handle(dev);
            if (handle == HALIDE_OPENGL_RENDER_TARGET) {
                bind_render_targets = false;
            }
            // Outbuf textures are handled explicitly below
            continue;
        } else if (kernel_arg->kind == Argument::Inbuf) {
            halide_assert(user_context, is_buffer[i] && "OpenGL Inbuf argument is not a buffer.")
            GLint loc =
                global_state.GetUniformLocation(kernel->program_id, kernel_arg->name);
            if (global_state.CheckAndReportError(user_context, "halide_opengl_run GetUniformLocation(InBuf)")) {
                return 1;
            }
            if (loc == -1) {
                error(user_context) << "No sampler defined for input texture.";
                return 1;
            }
            uint64_t dev = *(uint64_t *)args[i];
            if (!dev) {
                error(user_context) << "GLSL: Encountered invalid NULL dev pointer";
                return 1;
            }
            uint64_t handle = halide_get_device_handle(dev);
            global_state.ActiveTexture(GL_TEXTURE0 + num_active_textures);
            global_state.BindTexture(GL_TEXTURE_2D, handle == HALIDE_OPENGL_RENDER_TARGET ? 0 : (GLuint)handle);
            global_state.Uniform1iv(loc, 1, &num_active_textures);

            // Textures not created by the Halide runtime might not have
            // parameters set, or might have had parameters set differently
            global_state.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            global_state.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            global_state.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            global_state.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

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
                        << "OpenGL: argument '" << kernel_arg->name << "' is too large for GLint";
                    return -1;
                }
                uniform_int[uniform_int_idx++] = static_cast<GLint>(value);
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

    // Set the packed uniform int parameters
    for (int idx = 0; idx != num_padded_uniform_ints; idx += 4) {

        // Produce the uniform parameter name without using the std library.
        Printer<StringStreamPrinter,16> name(user_context);
        name << "_uniformi" << (idx/4);

        GLint loc = global_state.GetUniformLocation(kernel->program_id, name.str());
        if (global_state.CheckAndReportError(user_context, "halide_opengl_run GetUniformLocation")) {
            return 1;
        }
        if (loc == -1) {
            // Argument was probably optimized away by GLSL compiler.
            continue;
        }

        global_state.Uniform4iv(loc,1,&uniform_int[idx]);
    }

    // Set the packed uniform float parameters
    for (int idx = 0; idx != num_padded_uniform_floats; idx += 4) {

        // Produce the uniform parameter name without using the std library.
        Printer<StringStreamPrinter,16> name(user_context);
        name << "_uniformf" << (idx/4);

        GLint loc = global_state.GetUniformLocation(kernel->program_id, name.str());
        if (global_state.CheckAndReportError(user_context, "halide_opengl_run GetUniformLocation(2)")) {
            return 1;
        }
        if (loc == -1) {
            // Argument was probably optimized away by GLSL compiler.
            continue;
        }

        global_state.Uniform4fv(loc,1,&uniform_float[idx]);
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

        halide_assert(user_context, is_buffer[i] && "OpenGL Outbuf argument is not a buffer.")

        // TODO: GL_MAX_COLOR_ATTACHMENTS
        if (num_output_textures >= 1) {
            error(user_context)
                << "OpenGL ES 2.0 only supports one single output texture";
            return 1;
        }

        uint64_t dev = *(uint64_t *)args[i];
        if (!dev) {
            error(user_context) << "GLSL: Encountered invalid NULL dev pointer";
            return 1;
        }
        uint64_t handle = halide_get_device_handle(dev);
        GLuint tex = (handle == HALIDE_OPENGL_RENDER_TARGET) ? 0 : (GLuint)handle;

        // Check to see if the object name is actually a FBO
        if (bind_render_targets) {
            debug(user_context)
                << "Output texture " << num_output_textures << ": " << tex << "\n";
            global_state.FramebufferTexture2D(GL_FRAMEBUFFER,
                                    GL_COLOR_ATTACHMENT0 + num_output_textures,
                                    GL_TEXTURE_2D, tex, 0);
            if (global_state.CheckAndReportError(user_context, "halide_opengl_run FramebufferTexture2D")) {
                return 1;
            }
        }

        TextureInfo *texinfo = find_texture_info(tex);
        if (!texinfo) {
            error(user_context) << "Undefined output texture " << tex;
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
        error(user_context) << "halide_opengl_run: kernel has no output\n";
        // TODO: cleanup
        return 1;
    } else if (num_output_textures > 1) {
        if (global_state.DrawBuffers) {
            HalideMalloc draw_buffers_tmp(user_context, num_output_textures * sizeof(GLenum));
            if (!draw_buffers_tmp.ptr) {
                error(user_context) << "halide_malloc";
                return 1;
            }
            GLenum *draw_buffers = (GLenum*)draw_buffers_tmp.ptr;
            for (int i=0; i<num_output_textures; i++) {
                draw_buffers[i] = GL_COLOR_ATTACHMENT0 + i;
            }
            global_state.DrawBuffers(num_output_textures, draw_buffers);
            if (global_state.CheckAndReportError(user_context, "halide_opengl_run DrawBuffers")) {
                return 1;
            }
        } else {
            error(user_context) << "halide_opengl_run: kernel has more than one output and DrawBuffers is not available (earlier than GL ES 3.0?).\n";
            // TODO: cleanup
            return 1;
        }
    }

    if (bind_render_targets) {
        // Check that framebuffer is set up correctly
        GLenum status = global_state.CheckFramebufferStatus(GL_FRAMEBUFFER);
        if (global_state.CheckAndReportError(user_context, "halide_opengl_run CheckFramebufferStatus")) {
            return 1;
        }
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
    if (global_state.CheckAndReportError(user_context, "halide_opengl_run Uniform2iv(output_extent)")) {
        return 1;
    }
    loc = global_state.GetUniformLocation(kernel->program_id, "output_min");
    global_state.Uniform2iv(loc, 1, output_min);
    if (global_state.CheckAndReportError(user_context, "halide_opengl_run Uniform2iv(output_min)")) {
        return 1;
    }

#if 0 // DEBUG_RUNTIME
    debug(user_context) << "output_extent: " << output_extent[0] << "," << output_extent[1] << "\n";
    debug(user_context) << "output_min: " << output_min[0] << "," << output_min[1] << "\n";
#endif

    // TODO(abestephensg): Sort coordinate dimensions when the linear solver is integrated
    // Sort the coordinates

    // Construct an element buffer using the sorted vertex order.
    // Note that this is "width" and "height" of the vertices, not the output image.
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
    global_state.Viewport(0, 0, output_extent[0], output_extent[1]);

    // Setup the vertex and element buffers
    GLuint vertex_array_object = 0;
    if (global_state.have_vertex_array_objects) {
        global_state.GenVertexArrays(1, &vertex_array_object);
        global_state.BindVertexArray(vertex_array_object);
    }

    GLuint vertex_buffer_id;
    global_state.GenBuffers(1, &vertex_buffer_id);
    global_state.BindBuffer(GL_ARRAY_BUFFER, vertex_buffer_id);
    global_state.BufferData(GL_ARRAY_BUFFER, sizeof(float)*vertex_buffer_size, vertex_buffer, GL_STATIC_DRAW);
    if (global_state.CheckAndReportError(user_context, "halide_opengl_run vertex BufferData et al")) {
        return 1;
    }

    GLuint element_buffer_id;
    global_state.GenBuffers(1, &element_buffer_id);
    global_state.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer_id);
    global_state.BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(float)*element_buffer_size, element_buffer, GL_STATIC_DRAW);
    if (global_state.CheckAndReportError(user_context, "halide_opengl_run element BufferData et al")) {
        return 1;
    }

    // The num_padded_attributes argument is the number of vertex attributes,
    // including the spatial x and y coordinates, padded up to a multiple of
    // four so that the attributes may be packed into vec4 slots.
    int num_packed_attributes = num_padded_attributes/4;

    // Set up the per vertex attributes
    GLint attrib_ids[num_packed_attributes];

    for (int i=0;i!=num_packed_attributes;i++) {

        // The attribute names can synthesized by the runtime based on the
        // number of packed varying attributes
        Printer<StringStreamPrinter> attribute_name(user_context);
        attribute_name << "_varyingf" << i << "_attrib";

        // TODO(abstephensg): Switch to glBindAttribLocation
        GLint attrib_id = global_state.GetAttribLocation(kernel->program_id, attribute_name.buf);
        attrib_ids[i] = attrib_id;

        // Check to see if the varying attribute was simplified out of the
        // program by the GLSL compiler.
        if (attrib_id == -1) {
          continue;
        }

        global_state.VertexAttribPointer(attrib_id, 4, GL_FLOAT, GL_FALSE /* Normalized */, sizeof(GLfloat)*num_padded_attributes, (void*)(i*sizeof(GLfloat)*4));
        if (global_state.CheckAndReportError(user_context, "halide_opengl_run VertexAttribPointer et al")) {
            return 1;
        }

        global_state.EnableVertexAttribArray(attrib_id);
        if (global_state.CheckAndReportError(user_context, "halide_opengl_run EnableVertexAttribArray et al")) {
            return 1;
        }
    }


    // Draw the scene
    global_state.DrawElements(GL_TRIANGLES, element_buffer_size, GL_UNSIGNED_INT, NULL);
    if (global_state.CheckAndReportError(user_context, "halide_opengl_run DrawElements et al")) {
        return 1;
    }

    // Cleanup
    if (global_state.have_vertex_array_objects) {
        global_state.DeleteVertexArrays(1, &vertex_array_object);
    }

    global_state.DeleteBuffers(1, &vertex_buffer_id);
    global_state.DeleteBuffers(1, &element_buffer_id);

    return 0;
}

WEAK int halide_opengl_device_sync(void *user_context, struct buffer_t *) {
    if (!global_state.initialized) {
        error(user_context) << "OpenGL runtime not initialized (halide_opengl_device_sync).";
        return 1;
    }
#ifdef DEBUG_RUNTIME
    int64_t t0 = halide_current_time_ns(user_context);
#endif
    global_state.Finish();
#ifdef DEBUG_RUNTIME
    int64_t t1 = halide_current_time_ns(user_context);
    debug(user_context) << "halide_opengl_device_sync: took " << (t1 - t0) / 1e3 << "usec\n";
#endif
    return 0;
}

// Called at the beginning of a code block generated by Halide. This function
// is responsible for setting up the OpenGL environment and compiling the GLSL
// code into a fragment shader.
WEAK int halide_opengl_initialize_kernels(void *user_context, void **state_ptr,
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

        // Create the vertex shader. The runtime will output boilerplate for the
        // vertex shader based on a fixed program plus arguments obtained from
        // the comment header passed in the fragment shader. Since there are a
        // relatively small number of vertices (i.e. usually only four), per-vertex
        // expressions interpolated by varying attributes are evaluated
        // by host code on the CPU and passed to the GPU as values in the
        // vertex buffer.
        enum { PrinterLength = 1024*4 };
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
        GLuint program = global_state.CreateProgram();
        global_state.AttachShader(program, vertex_shader_id);
        global_state.AttachShader(program, fragment_shader_id);
        global_state.LinkProgram(program);

        // Release the individual shaders
        global_state.DeleteShader(vertex_shader_id);
        global_state.DeleteShader(fragment_shader_id);

        GLint status;
        global_state.GetProgramiv(program, GL_LINK_STATUS, &status);
        if (!status) {
            GLint log_len;
            global_state.GetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
            HalideMalloc log_tmp(user_context, log_len);
            if (log_tmp.ptr) {
                char *log = (char*) log_tmp.ptr;
                global_state.GetProgramInfoLog(program, log_len, NULL, log);
                debug(user_context) << "Could not link GLSL program:\n" << log << "\n";
            }
            global_state.DeleteProgram(program);
            return -1;
        }
        kernel->program_id = program;
    }
    return 0;
}

WEAK int halide_opengl_device_and_host_malloc(void *user_context, struct buffer_t *buf) {
    return halide_default_device_and_host_malloc(user_context, buf, &opengl_device_interface);
}

WEAK int halide_opengl_device_and_host_free(void *user_context, struct buffer_t *buf) {
    return halide_default_device_and_host_free(user_context, buf, &opengl_device_interface);
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

WEAK int halide_opengl_wrap_texture(void *user_context, struct buffer_t *buf, uintptr_t texture_id) {
  if (!global_state.initialized) {
      // Must initialize here: if not, we risk having the TextureInfo
      // blown away when global state really is inited.
      if (int error = halide_opengl_init(user_context)) {
          return error;
      }
    }
    if (texture_id == 0) {
        error(user_context) << "Texture " << texture_id << " is not a valid texture name.";
        return -3;
    }
    halide_assert(user_context, buf->dev == 0);
    if (buf->dev != 0) {
        return -2;
    }
    if (find_texture_info(texture_id)) {
        error(user_context) << "Internal error: texture " << texture_id << " is already wrapped.";
        return -3;
    }
    (void) new_texture_info(texture_id, buf->min, buf->extent, /* halide_allocated */ false);
    buf->dev = halide_new_device_wrapper(texture_id, &opengl_device_interface);
    if (buf->dev == 0) {
        return -1;
    }
    return 0;
}

WEAK int halide_opengl_wrap_render_target(void *user_context, struct buffer_t *buf) {
    if (!global_state.initialized) {
        // Must initialize here: if not, we risk having the TextureInfo
        // blown away when global state really is inited.
        if (int error = halide_opengl_init(user_context)) {
            return error;
        }
    }
    halide_assert(user_context, buf->dev == 0);
    if (buf->dev != 0) {
        return -2;
    }
    const GLuint tex = 0;
    if (find_texture_info(tex)) {
        error(user_context) << "Internal error: texture " << tex << " is already wrapped.";
        return -3;
    }
    (void) new_texture_info(tex, buf->min, buf->extent, /* halide_allocated */ false);
    buf->dev = halide_new_device_wrapper(HALIDE_OPENGL_RENDER_TARGET, &opengl_device_interface);
    if (buf->dev == 0) {
        return -1;
    }
    return 0;
}

WEAK uintptr_t halide_opengl_detach_texture(void *user_context, struct buffer_t *buf) {
    if (buf->dev == NULL) {
        return 0;
    }
    halide_assert(user_context, halide_get_device_interface(buf->dev) == &opengl_device_interface);
    uint64_t handle = halide_get_device_handle(buf->dev);
    halide_delete_device_wrapper(buf->dev);
    buf->dev = 0;
    GLuint tex = (handle == HALIDE_OPENGL_RENDER_TARGET) ? 0 : handle;
    TextureInfo *texinfo = unlink_texture_info(tex);
    if (!texinfo) {
        error(user_context) << "Internal error: texture " << tex << " not found.";
        return -3;
    }
    halide_assert(user_context, !texinfo->halide_allocated);
    free(texinfo);
    return (uintptr_t) tex;
}

WEAK uintptr_t halide_opengl_get_texture(void *user_context, struct buffer_t *buf) {
    if (buf->dev == NULL) {
        return 0;
    }
    halide_assert(user_context, halide_get_device_interface(buf->dev) == &opengl_device_interface);
    uint64_t handle = halide_get_device_handle(buf->dev);
    // client_bound always return 0 here.
    return handle == HALIDE_OPENGL_RENDER_TARGET ? 0 : (uintptr_t)handle;
}

namespace {
__attribute__((destructor))
WEAK void halide_opengl_cleanup() {
    halide_opengl_device_release(NULL);
}
}

} // extern "C"


namespace Halide { namespace Runtime { namespace Internal { namespace OpenGL {

WEAK halide_device_interface opengl_device_interface = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_opengl_device_malloc,
    halide_opengl_device_free,
    halide_opengl_device_sync,
    halide_opengl_device_release,
    halide_opengl_copy_to_host,
    halide_opengl_copy_to_device,
    halide_opengl_device_and_host_malloc,
    halide_opengl_device_and_host_free,
};

}}}} // namespace Halide::Runtime::Internal::OpenGL
