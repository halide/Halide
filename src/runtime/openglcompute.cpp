#include "runtime_internal.h"
#include "device_interface.h"
#include "HalideRuntimeOpenGLCompute.h"
#include "mini_opengl.h"

// This constant is used to indicate that the application will take
// responsibility for binding the output render target before calling the
// Halide function.
#define HALIDE_OPENGLCOMPUTE_RENDER_TARGET ((uint64_t)-1)

// Implementation note: all function that directly or indirectly access the
// runtime state in halide_openglcompute_state must be declared as WEAK, otherwise
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
    GLFUNC(PFNGLUNIFORM1FPROC, Uniform1f);                              \
    GLFUNC(PFNGLUNIFORM1IPROC, Uniform1i);                              \
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
    GLFUNC(PFNGLFINISHPROC, Finish);

// List of all OpenGL functions used by the runtime, which may not
// exist due to an older or less capable version of GL. In using any
// of these functions, code must test if they are NULL.
#define OPTIONAL_GL_FUNCTIONS                                           \
    GLFUNC(PFNGLGENVERTEXARRAYS, GenVertexArrays);                      \
    GLFUNC(PFNGLBINDVERTEXARRAY, BindVertexArray);                      \
    GLFUNC(PFNGLDELETEVERTEXARRAYS, DeleteVertexArrays);                \
    GLFUNC(PFNDRAWBUFFERS, DrawBuffers);                                \
    \
    GLFUNC(PFNGLTEXBUFFEREXTPROC, TexBufferEXT);                        \
    GLFUNC(PFNGLBINDIMAGETEXTUREPROC, BindImageTexture);                \
    GLFUNC(PFNGLMEMORYBARRIERPROC, MemoryBarrier);                      \
    GLFUNC(PFNGLMAPBUFFERRANGEPROC, MapBufferRange);                    \
    GLFUNC(PFNGLDISPATCHCOMPUTEPROC, DispatchCompute);

// ---------- Types ----------

using namespace Halide::Runtime::Internal;

namespace Halide { namespace Runtime { namespace Internal { namespace OpenGLCompute {

extern WEAK halide_device_interface openglcompute_device_interface;

WEAK const char *gl_error_name(int32_t err) {
  switch (err) {
      case 0x500: return "GL_INVALID_ENUM"; break;
      case 0x501: return "GL_INVALID_VALUE"; break;
      case 0x502: return "GL_INVALID_OPERATION"; break;
      case 0x503: return "GL_STACK_OVERFLOW"; break;
      case 0x504: return "GL_STACK_UNDERFLOW"; break;
      case 0x505: return "GL_OUT_OF_MEMORY"; break;
      case 0x506: return "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
      case 0x507: return "GL_CONTEXT_LOST"; break;
      case 0x8031: return "GL_TABLE_TOO_LARGE"; break;
  }
  return "<unknown GL error>";
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

struct KernelInfo {
    char *source;
    GLuint shader_id;
    GLuint program_id;
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

// A list of module-specific state. Each module corresponds to a single Halide filter
WEAK ModuleState *state_list;

// ---------- Helper functions ----------

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

WEAK void GlobalState::init() {
    initialized = false;
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

// Initialize the OpenGL-specific parts of the runtime.
WEAK int halide_openglcompute_init(void *user_context) {
    if (global_state.initialized) {
        return 0;
    }

    global_state.init();

    // Make a context if there isn't one
    if (halide_opengl_create_context(user_context)) {
        error(user_context) << "Failed to make OpenGL context";
        return -1;
    }

    // Initialize pointers to core and optional OpenGL functions.
#define GLFUNC(TYPE, VAR)                                                             \
    if (load_gl_func(user_context, "gl" #VAR, (void**)&global_state.VAR, true) < 0) { \
        return -1;                                                                    \
    }
    USED_GL_FUNCTIONS;
    OPTIONAL_GL_FUNCTIONS;
#undef GLFUNC

    const char *version = (const char *)global_state.GetString(GL_VERSION);

    debug(user_context) << "Halide running on OpenGL " << version << "\n";

    global_state.initialized = true;
    return 0;
}

// Release all data allocated by the runtime.
//
// The OpenGL context itself is generally managed by the host application, so
// we leave it untouched.
WEAK int halide_openglcompute_device_release(void *user_context) {
    if (!global_state.initialized) {
        return 0;
    }

    //TODO(aam): implement this.
    debug(user_context) << "halide_openglcompute_release not implemented\n";

    global_state = GlobalState();

    return 0;
}

WEAK size_t buf_size(void *user_context, buffer_t *buf) {
    size_t size = buf->elem_size;
    for (size_t i = 0; i < sizeof(buf->stride) / sizeof(buf->stride[0]); i++) {
        size_t total_dim_size =
            buf->elem_size * buf->extent[i] * buf->stride[i];
        if (total_dim_size > size) {
            size = total_dim_size;
        }
    }
    halide_assert(user_context, size);
    return size;
}

// Keep texture buffer object and buffer in single 64-bit word.
uint64_t pack_tbo_and_buffer(GLuint the_tbo, GLuint the_buffer) {
    return (((uint64_t)the_tbo) << 32) | the_buffer;
}
GLuint unpack_tbo(uint64_t packed) { return packed >> 32; }
GLuint unpack_buffer(uint64_t packed) { return packed & 0xffffffff; }

// Allocate a new texture matching the dimension and color format of the
// specified buffer.
WEAK int halide_openglcompute_device_malloc(void *user_context, buffer_t *buf) {
    debug(user_context) << "OpenGLCompute: halide_openglcompute_device_malloc (user_context: "
                        << user_context << ", buf: " << buf << ")\n";

    if (!global_state.initialized) {
        error(user_context) << "OpenGL runtime not initialized (halide_openglcompute_copy_to_device).";
        return 1;
    }

    size_t size = buf_size(user_context, buf);

    if (buf->dev) {
        // This buffer already has a device allocation
        debug(user_context) << "openglcompute_device_malloc: This buffer already has a "
                               "device allocation\n";
        return 0;
    }

    halide_assert(user_context, buf->stride[0] >= 0 && buf->stride[1] >= 0 &&
                                buf->stride[2] >= 0 && buf->stride[3] >= 0);

    debug(user_context) << "    allocating "
                        << " buffer of " << (int64_t)size << " bytes, "
                        << "extents: " << buf->extent[0] << "x"
                        << buf->extent[1] << "x" << buf->extent[2] << "x"
                        << buf->extent[3] << " "
                        << "strides: " << buf->stride[0] << "x"
                        << buf->stride[1] << "x" << buf->stride[2] << "x"
                        << buf->stride[3] << " "
                        << "(" << buf->elem_size << " bytes per element)\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    if (int error = halide_openglcompute_init(user_context)) {
        return error;
    }
    debug(user_context) << "openglcompute_device_malloc: initialization completed.\n";

    if (!buf) {
        error(user_context) << "Invalid buffer";
        return 1;
    }

    GLuint the_buffer;
    global_state.GenBuffers(1, &the_buffer);
    if (global_state.CheckAndReportError(user_context, "oglc: GenBuffers")) { return 1; }
    global_state.BindBuffer(GL_TEXTURE_BUFFER_EXT, the_buffer);
    if (global_state.CheckAndReportError(user_context, "oglc: BindBuffer")) { return 1; }

    size_t sizeInBytes = buf_size(user_context, buf);
    global_state.BufferData(GL_TEXTURE_BUFFER_EXT, sizeInBytes, NULL, GL_DYNAMIC_COPY);
    if (global_state.CheckAndReportError(user_context, "oglc: BufferData")) { return 1; }

    GLuint the_tbo;
    global_state.GenTextures(1, &the_tbo);
    if (global_state.CheckAndReportError(user_context, "oglc: GenTextures")) { return 1; }
    global_state.BindTexture(GL_TEXTURE_BUFFER_EXT, the_tbo);
    if (global_state.CheckAndReportError(user_context, "oglc: BindTexture")) { return 1; }
    // TODO(aam): Support formats other than RGBA32F
    global_state.TexBufferEXT(GL_TEXTURE_BUFFER_EXT, GL_RGBA32F, the_buffer);
    if (global_state.CheckAndReportError(user_context, "oglc: TexBufferEXT")) { return 1; }
    global_state.BindTexture(GL_TEXTURE_BUFFER_EXT, 0);
    if (global_state.CheckAndReportError(user_context, "oglc: BindTexture")) { return 1; }

    buf->dev = halide_new_device_wrapper(
        pack_tbo_and_buffer(the_tbo, the_buffer),
        &openglcompute_device_interface);

    debug(user_context) << "Allocated dev_buffer(i.e. tbo) " << the_tbo << "\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms\n";
#endif

    return 0;
}

// Delete all texture information associated with a buffer. The OpenGL texture
// itself is only deleted if it was actually allocated by Halide and not
// provided by the host application.
WEAK int halide_openglcompute_device_free(void *user_context, buffer_t *buf) {
    if (!global_state.initialized) {
        error(user_context) << "OpenGL runtime not initialized in call to halide_openglcompute_device_free.";
        return 1;
    }

    if (buf->dev == 0) {
        return 0;
    }

    int64_t packed = halide_get_device_handle(buf->dev);
    GLuint the_tbo = unpack_tbo(packed);
    GLuint the_buffer = unpack_buffer(packed);
    debug(user_context) << "OGLC: halide_openglcompute_device_free ("
                        << "user_context: " << user_context
                        << ", buf: " << buf
                        << ", the_tbo:" << the_tbo
                        << ", the_buffer:" << the_buffer << ")\n";

    // TODO(aam): implement this
    debug(user_context) << "not implemented halide_openglcompute_device_free\n";

    return 0;
}

// Copy image data from host memory to texture.
WEAK int halide_openglcompute_copy_to_device(void *user_context, buffer_t *buf) {
    if (!global_state.initialized) {
        error(user_context) << "OpenGL runtime not initialized (halide_openglcompute_copy_to_device).";
        return 1;
    }

    int64_t packed = halide_get_device_handle(buf->dev);
    GLuint the_tbo = unpack_tbo(packed);
    GLuint the_buffer = unpack_buffer(packed);
    debug(user_context) << "OGLC: halide_openglcompute_copy_to_device ("
                        << "user_context: " << user_context
                        << ", buf: " << buf
                        << ", the_tbo:" << the_tbo
                        << ", the_buffer:" << the_buffer << ")\n";

    global_state.BindBuffer(GL_TEXTURE_BUFFER_EXT, the_buffer);
    if (global_state.CheckAndReportError(user_context, "oglc: BindBuffer")) { return 1; }

    size_t size = buf_size(user_context, buf);
    global_state.BufferData(GL_TEXTURE_BUFFER_EXT, size, buf->host, GL_DYNAMIC_COPY);
    if (global_state.CheckAndReportError(user_context, "oglc: BufferData")) { return 1; }

    debug(user_context) << "  copied " << ((unsigned)size) << " bytes from " << buf->host << " to the device.\n";
    return 0;
}

// Copy image data from texture back to host memory.
WEAK int halide_openglcompute_copy_to_host(void *user_context, buffer_t *buf) {
    if (!global_state.initialized) {
        error(user_context) << "OpenGL runtime not initialized (halide_openglcompute_copy_to_host).";
        return 1;
    }

    int64_t packed = halide_get_device_handle(buf->dev);
    GLuint the_tbo = unpack_tbo(packed);
    GLuint the_buffer = unpack_buffer(packed);
    debug(user_context) << "OGLC: halide_openglcompute_copy_to_host ("
                        << "user_context: " << user_context
                        << ", buf: " << buf
                        << ", the_tbo:" << the_tbo
                        << ", the_buffer:" << the_buffer << ")\n";

    size_t size = buf_size(user_context, buf);

    global_state.BindBuffer(GL_TEXTURE_BUFFER_EXT, the_buffer);
    if (global_state.CheckAndReportError(user_context, "oglc: BindBuffer")) { return 1; }
    void* device_data = global_state.MapBufferRange(
        GL_TEXTURE_BUFFER_EXT, 0, size, GL_MAP_READ_BIT);
    if (global_state.CheckAndReportError(user_context, "oglc: MapBufferRange")) { return 1; }

    memcpy(buf->host, device_data, size);

    debug(user_context) << "  copied " << (unsigned)size << " to the host.\n";

    return 0;
}

}}}} // namespace Halide::Runtime::Internal::OpenGLCompute

using namespace Halide::Runtime::Internal::OpenGLCompute;

//  Create wrappers that satisfy old naming conventions

extern "C" {

WEAK int halide_openglcompute_run(void *user_context, void *state_ptr,
                       const char *entry_name, int blocksX, int blocksY,
                       int blocksZ, int threadsX, int threadsY, int threadsZ,
                       int shared_mem_bytes, size_t arg_sizes[], void *args[],
                       int8_t arg_is_buffer[], int num_attributes,
                       float *vertex_buffer, int num_coords_dim0,
                       int num_coords_dim1) {

    debug(user_context)
        << "OpenGLCompute: halide_openglcompute_run (user_context: " << user_context << ", "
        << "entry: " << entry_name << ", "
        << "blocks: " << blocksX << "x" << blocksY << "x" << blocksZ << ", "
        << "threads: " << threadsX << "x" << threadsY << "x" << threadsZ << ", "
        << "shmem: " << shared_mem_bytes  << ", "
        << "num_attributes: " << num_attributes << ", "
        << "num_coords_dim0: " << num_coords_dim0 << ", "
        << "num_coords_dim1: " << num_coords_dim1 << "\n";

    if (!global_state.initialized) {
        error(user_context) << "OpenGL runtime not initialized (halide_openglcompute_run).";
        return 1;
    }

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
    if (global_state.CheckAndReportError(user_context, "halide_openglcompute_run UseProgram")) {
        return 1;
    }

    // Populate uniforms with values passed in arguments.
    // Order of the passed arguments matches what was generated for this kernel.
    int i = 0;
    while (arg_sizes[i] != 0) {
        debug(user_context) << "    args " << i
                            << " " << (int)arg_sizes[i]
                            << " [" << (*((void **)args[i])) << " ...] "
                            << arg_is_buffer[i] << "\n";
        if (arg_is_buffer[i] == 0) {
            // TODO(aam): Support types other than int
            int value = *((int *)args[i]);
            global_state.Uniform1i(i, value);
            if (global_state.CheckAndReportError(user_context, "halide_openglcompute_run Uniform1i")) { return 1; }
        } else {
            uint64_t arg_value = *(uint64_t *)args[i];

            bool isThisTheLastArgument = arg_sizes[i+1] == 0;
            GLuint the_tbo = (GLuint)halide_get_device_handle(arg_value);
            global_state.BindImageTexture(i, the_tbo, 0, false, 0, isThisTheLastArgument? GL_WRITE_ONLY: GL_READ_ONLY, GL_RGBA32F);
            if (global_state.CheckAndReportError(user_context, "halide_openglcompute_run BindImageTexture")) { return 1; }
        }
        i++;
    }
    // TODO(aam): support multi-dimensional dispatches
    // TODO(aam): why this value makes sense?
    const int workgroups_x = 1024;
    global_state.DispatchCompute(workgroups_x, 1, 1);
    if (global_state.CheckAndReportError(user_context, "halide_openglcompute_run DispatchCompute")) { return 1; }

    return 0;
}

WEAK int halide_openglcompute_device_sync(void *user_context, struct buffer_t *) {
    if (!global_state.initialized) {
        error(user_context) << "OpenGL Compute runtime not initialized (halide_openglcompute_device_sync).";
        return 1;
    }
    global_state.Finish();
    return 0;
}

// Called at the beginning of a code block generated by Halide. This function
// is responsible for setting up the OpenGL environment and compiling the GLSL
// code into a fragment shader.
WEAK int halide_openglcompute_initialize_kernels(void *user_context, void **state_ptr,
                                          const char *src, int size) {
    if (int error = halide_openglcompute_init(user_context)) {
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
        kernel = (KernelInfo *)malloc(sizeof(KernelInfo));
        module->kernel = kernel;

        GLuint shader = global_state.CreateShader(GL_COMPUTE_SHADER);
        if (global_state.CheckAndReportError(user_context, "create shader")) { return -1; }
        const GLchar* sources = { src };

        debug(user_context) << "Compute shader source:" << src << "\n";

        global_state.ShaderSource(shader, 1, &sources, NULL);
        if (global_state.CheckAndReportError(user_context, "shader source")) { return -1; }
        global_state.CompileShader(shader);
        if (global_state.CheckAndReportError(user_context, "compile shader")) { return -1; }

        GLint shader_ok = 0;
        global_state.GetShaderiv(shader, GL_COMPILE_STATUS, &shader_ok);
        if (shader_ok != GL_TRUE) {
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
            return -1;
        }

        // Link GLSL program
        GLuint program = global_state.CreateProgram();
        global_state.AttachShader(program, shader);
        if (global_state.CheckAndReportError(user_context, "attach shader")) { return -1; }
        global_state.LinkProgram(program);
        if (global_state.CheckAndReportError(user_context, "link program")) { return -1; }

        // Release the individual shaders
        global_state.DeleteShader(shader);

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

WEAK const struct halide_device_interface *halide_openglcompute_device_interface() {
    return &openglcompute_device_interface;
}



} // extern "C"


namespace Halide { namespace Runtime { namespace Internal { namespace OpenGLCompute {

WEAK halide_device_interface openglcompute_device_interface = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_openglcompute_device_malloc,
    halide_openglcompute_device_free,
    halide_openglcompute_device_sync,
    halide_openglcompute_device_release,
    halide_openglcompute_copy_to_host,
    halide_openglcompute_copy_to_device,
};

}}}} // namespace Halide::Runtime::Internal::OpenGLCompute
