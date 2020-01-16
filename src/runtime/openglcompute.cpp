#include "HalideRuntimeOpenGLCompute.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "mini_opengl.h"
#include "printer.h"

// Implementation note: all function that directly or indirectly access the
// runtime state in halide_openglcompute_state must be declared as WEAK, otherwise
// the behavior at runtime is undefined.

// List of all OpenGL functions used by the runtime. The list is used to
// declare and initialize the dispatch table in OpenGLState below.
//
// grep "global_state." ../../src/runtime/openglcompute.cpp | sed -n "s/^\(.*\)global_state\.\([^(]*\).*/\2/p" | sort | uniq
//  +GetError, GetString
//  -CheckAndReportError
//
#define USED_GL_FUNCTIONS                                  \
    GLFUNC(PFNGLATTACHSHADERPROC, AttachShader);           \
    GLFUNC(PFNGLBINDBUFFERPROC, BindBuffer);               \
    GLFUNC(PFNGLBINDBUFFERBASEPROC, BindBufferBase);       \
    GLFUNC(PFNGLBUFFERDATAPROC, BufferData);               \
    GLFUNC(PFNGLCREATEPROGRAMPROC, CreateProgram);         \
    GLFUNC(PFNGLCOMPILESHADERPROC, CompileShader);         \
    GLFUNC(PFNGLCREATESHADERPROC, CreateShader);           \
    GLFUNC(PFNGLDELETEBUFFERSPROC, DeleteBuffers);         \
    GLFUNC(PFNGLDELETEPROGRAMPROC, DeleteProgram);         \
    GLFUNC(PFNGLDELETESHADERPROC, DeleteShader);           \
    GLFUNC(PFNGLDISPATCHCOMPUTEPROC, DispatchCompute);     \
    GLFUNC(PFNGLFINISHPROC, Finish);                       \
    GLFUNC(PFNGLGENBUFFERSPROC, GenBuffers);               \
    GLFUNC(PFNGLGETERRORPROC, GetError);                   \
    GLFUNC(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog); \
    GLFUNC(PFNGLGETPROGRAMIVPROC, GetProgramiv);           \
    GLFUNC(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog);   \
    GLFUNC(PFNGLGETSHADERIVPROC, GetShaderiv);             \
    GLFUNC(PFNGLGETSTRINGPROC, GetString);                 \
    GLFUNC(PFNGLLINKPROGRAMPROC, LinkProgram);             \
    GLFUNC(PFNGLMAPBUFFERRANGEPROC, MapBufferRange);       \
    GLFUNC(PFNGLMEMORYBARRIERPROC, MemoryBarrier);         \
    GLFUNC(PFNGLSHADERSOURCEPROC, ShaderSource);           \
    GLFUNC(PFNGLUNIFORM1IPROC, Uniform1i);                 \
    GLFUNC(PFNGLUNIFORM1IPROC, Uniform1ui);                \
    GLFUNC(PFNGLUNIFORM1FPROC, Uniform1f);                 \
    GLFUNC(PFNGLUNMAPBUFFERPROC, UnmapBuffer);             \
    GLFUNC(PFNGLUSEPROGRAMPROC, UseProgram);               \
    GLFUNC(PFNGLGETACTIVEUNIFORM, GetActiveUniform);       \
    GLFUNC(PFNGLGETUNIFORMLOCATION, GetUniformLocation);

using namespace Halide::Runtime::Internal;

namespace Halide {
namespace Runtime {
namespace Internal {
namespace OpenGLCompute {

extern WEAK halide_device_interface_t openglcompute_device_interface;

WEAK const char *gl_error_name(int32_t err) {
    switch (err) {
    case 0x500:
        return "GL_INVALID_ENUM";
        break;
    case 0x501:
        return "GL_INVALID_VALUE";
        break;
    case 0x502:
        return "GL_INVALID_OPERATION";
        break;
    case 0x503:
        return "GL_STACK_OVERFLOW";
        break;
    case 0x504:
        return "GL_STACK_UNDERFLOW";
        break;
    case 0x505:
        return "GL_OUT_OF_MEMORY";
        break;
    case 0x506:
        return "GL_INVALID_FRAMEBUFFER_OPERATION";
        break;
    case 0x507:
        return "GL_CONTEXT_LOST";
        break;
    case 0x8031:
        return "GL_TABLE_TOO_LARGE";
        break;
    }
    return "<unknown GL error>";
}

struct HalideMalloc {
    __attribute__((always_inline)) HalideMalloc(void *user_context, size_t size)
        : user_context(user_context), ptr(halide_malloc(user_context, size)) {
    }
    __attribute__((always_inline)) ~HalideMalloc() {
        halide_free(user_context, ptr);
    }
    void *const user_context;
    void *const ptr;
};

struct KernelInfo {
    char *kernel_name;
    GLuint program_id;
    KernelInfo *next;
};

struct ModuleState {
    KernelInfo *kernel;
    ModuleState *next;
};

WEAK KernelInfo *find_kernel_by_name(const char *kernel_name, const ModuleState *module) {
    KernelInfo *kernel = module->kernel;
    while (kernel && strcmp(kernel_name, kernel->kernel_name) != 0) {
        kernel = kernel->next;
    }
    return kernel;
}

// All persistent state maintained by the runtime.
struct GlobalState {
    void init();
    bool CheckAndReportError(void *user_context, const char *location);

    bool initialized;

    // Declare pointers used OpenGL functions
#define GLFUNC(PTYPE, VAR) PTYPE VAR
    USED_GL_FUNCTIONS;
#undef GLFUNC
};

WEAK bool GlobalState::CheckAndReportError(void *user_context, const char *location) {
    GLenum err = GetError();
    if (err != GL_NO_ERROR) {
        error(user_context)
            << "OpenGL error " << gl_error_name(err)
            << "(" << (int)err << ")"
            << " at " << location << ".\n";
        return true;
    }
    return false;
}

WEAK GlobalState global_state;

// A list of module-specific state. Each module corresponds to a single Halide filter
WEAK ModuleState *state_list;

// ---------- Helper functions ----------

WEAK void debug_buffer(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "  device: " << buf->device << "\n"
        << "  texture_id: " << (GLuint)buf->device << "\n"
        << "  host: " << buf->host << "\n"
        << "  extent: " << buf->dim[0].extent << " " << buf->dim[1].extent
        << " " << buf->dim[2].extent << " " << buf->dim[3].extent << "\n"
        << "  stride: " << buf->dim[0].stride << " " << buf->dim[1].stride
        << " " << buf->dim[2].stride << " " << buf->dim[3].stride << "\n"
        << "  min: " << buf->dim[0].min << " " << buf->dim[1].min
        << " " << buf->dim[2].min << " " << buf->dim[3].min << "\n"
        << "  type: " << buf->type << "\n"
        << "  host_dirty: " << buf->host_dirty() << "\n"
        << "  device_dirty: " << buf->device_dirty() << "\n";
}

WEAK void GlobalState::init() {
    initialized = false;
#define GLFUNC(type, name) name = NULL;
    USED_GL_FUNCTIONS;
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

    // Initialize pointers to OpenGL functions.
#define GLFUNC(TYPE, VAR)                                                              \
    if (load_gl_func(user_context, "gl" #VAR, (void **)&global_state.VAR, true) < 0) { \
        return -1;                                                                     \
    }
    USED_GL_FUNCTIONS;
#undef GLFUNC

    debug(user_context) << "Halide running on " << global_state.GetString(GL_VERSION) << "\n";

    global_state.initialized = true;
    return 0;
}

// Release all data allocated by the runtime.
//
// The OpenGL context itself is generally managed by the host application, so
// we leave it untouched.
WEAK int halide_openglcompute_device_release(void *user_context) {
#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    debug(user_context) << "OpenGLCompute: halide_openglcompute_device_release(user_context: "
                        << user_context << ")\n";

    ModuleState *mod = state_list;
    while (mod) {
        KernelInfo *kernel = mod->kernel;
        while (kernel) {
            KernelInfo *next_kernel = kernel->next;
            global_state.DeleteProgram(kernel->program_id);
            free(kernel->kernel_name);
            free(kernel);
            kernel = next_kernel;
        }
        mod->kernel = NULL;
        ModuleState *next = mod->next;
        // do not call free(mod) to avoid dangling pointers: the module state
        // is still referenced in the code generated by Halide (see
        // CodeGen_GPU_Host::get_module_state).
        mod = next;
    }

    global_state = GlobalState();

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms\n";
#endif

    return 0;
}

// Allocate a new texture matching the dimension and color format of the
// specified buffer.
WEAK int halide_openglcompute_device_malloc(void *user_context, halide_buffer_t *buf) {
#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    debug(user_context) << "OpenGLCompute: halide_openglcompute_device_malloc (user_context: "
                        << user_context << ", buf: " << buf << ")\n";

    if (int error = halide_openglcompute_init(user_context)) {
        return error;
    }

    size_t size = buf->size_in_bytes();
    halide_assert(user_context, size != 0);

    if (buf->device) {
        // This buffer already has a device allocation
        debug(user_context) << "openglcompute_device_malloc: This buffer already has a "
                               "device allocation\n";
        return 0;
    }

    for (int i = 0; i < buf->dimensions; i++) {
        halide_assert(user_context, buf->dim[i].stride >= 0);
    }

    debug(user_context) << "    allocating buffer, "
                        << "extents: " << buf->dim[0].extent << "x"
                        << buf->dim[1].extent << "x" << buf->dim[2].extent << "x"
                        << buf->dim[3].extent << " "
                        << "strides: " << buf->dim[0].stride << "x"
                        << buf->dim[1].stride << "x" << buf->dim[2].stride << "x"
                        << buf->dim[3].stride << " "
                        << "(type: " << buf->type << ")\n";

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
    if (global_state.CheckAndReportError(user_context, "oglc: GenBuffers")) {
        return 1;
    }
    global_state.BindBuffer(GL_ARRAY_BUFFER, the_buffer);
    if (global_state.CheckAndReportError(user_context, "oglc: BindBuffer")) {
        return 1;
    }
    // OpenGLCompute only supports int32 and float data types, both of which are 4 bytes.
    size_t size_in_bytes = buf->number_of_elements() * 4;
    halide_assert(user_context, size_in_bytes != 0);
    global_state.BufferData(GL_ARRAY_BUFFER, size_in_bytes, NULL, GL_DYNAMIC_COPY);
    if (global_state.CheckAndReportError(user_context, "oglc: BufferData")) {
        return 1;
    }

    buf->device = the_buffer;
    buf->device_interface = &openglcompute_device_interface;
    buf->device_interface->impl->use_module();

    debug(user_context) << "Allocated dev_buffer(i.e. vbo) " << the_buffer << "\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms for malloc\n";
#endif

    return 0;
}

WEAK int halide_openglcompute_device_free(void *user_context, halide_buffer_t *buf) {
#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    if (!global_state.initialized) {
        error(user_context) << "OpenGL runtime not initialized in call to halide_openglcompute_device_free.";
        return 1;
    }

    if (buf->device == 0) {
        return 0;
    }
    GLuint the_buffer = (GLuint)buf->device;

    debug(user_context) << "OGLC: halide_openglcompute_device_free ("
                        << "user_context: " << user_context
                        << ", the_buffer:" << the_buffer
                        << ")\n";

    global_state.DeleteBuffers(1, &the_buffer);

    buf->device = 0;
    buf->device_interface->impl->release_module();
    buf->device_interface = NULL;

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms for free\n";
#endif

    return 0;
}

namespace {

template<typename Source, typename Dest>
__attribute__((always_inline)) void converting_copy_memory_helper(const device_copy &copy, int d, int64_t src_off, int64_t dst_off) {
    // Skip size-1 dimensions
    while (d >= 0 && copy.extent[d] == 1)
        d--;

    if (d == -1) {
        const Source *from = (Source *)(copy.src + src_off);
        Dest *to = (Dest *)(copy.dst + dst_off);
        for (uint64_t index = 0; index < copy.chunk_size; index++) {
            *to++ = (Dest)*from++;
        }
    } else {
        for (uint64_t i = 0; i < copy.extent[d]; i++) {
            converting_copy_memory_helper<Source, Dest>(copy, d - 1, src_off, dst_off);
            src_off += copy.src_stride_bytes[d];
            dst_off += copy.dst_stride_bytes[d];
        }
    }
}

}  // namespace
// Copy image data from host memory to texture.
WEAK int halide_openglcompute_copy_to_device(void *user_context, halide_buffer_t *buf) {
#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    if (!global_state.initialized) {
        error(user_context) << "OpenGL runtime not initialized (halide_openglcompute_copy_to_device).";
        return 1;
    }

    GLuint the_buffer = (GLuint)buf->device;
    debug(user_context) << "OGLC: halide_openglcompute_copy_to_device ("
                        << "user_context: " << user_context
                        << ", buf: " << buf
                        << ", the_buffer:" << the_buffer << ")\n";

    global_state.BindBuffer(GL_ARRAY_BUFFER, the_buffer);
    if (global_state.CheckAndReportError(user_context, "oglc: BindBuffer")) {
        return 1;
    }

    size_t size = buf->number_of_elements() * 4;
    global_state.BindBuffer(GL_ARRAY_BUFFER, the_buffer);
    if (global_state.CheckAndReportError(user_context, "oglc: BindBuffer")) {
        return 1;
    }

    debug(user_context) << "Calling global_state.MapBufferRange(GL_ARRAY_BUFFER, 0, " << (uint64_t)size << ", GL_MAP_READ_BIT|GL_MAP_WRITE_BIT)\n";
    void *device_data = global_state.MapBufferRange(GL_ARRAY_BUFFER,
                                                    0,
                                                    size,
                                                    GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);
    if (global_state.CheckAndReportError(user_context, "oglc: MapBufferRange")) {
        return 1;
    }
    halide_buffer_t buf_copy = *buf;
    buf_copy.device = (uint64_t)device_data;
    device_copy dev_copy = make_host_to_device_copy(&buf_copy);

    if (buf->type.code == halide_type_int) {
        if (buf->type.bits == 8) {
            converting_copy_memory_helper<int8_t, float>(dev_copy, MAX_COPY_DIMS - 1, dev_copy.src_begin, 0);
        } else if (buf->type.bits == 16) {
            // Convert chunk_size in bytes to the number of items to be copied.
            // This doesn't happen for the 8-bit case because it would be a division by one,
            // and it doesn't happen for the 32-bit case as there is no data conversion and memcpy
            // is used.
            dev_copy.chunk_size /= 2;
            converting_copy_memory_helper<int16_t, float>(dev_copy, MAX_COPY_DIMS - 1, dev_copy.src_begin, 0);
        } else if (buf->type.bits == 32) {
            copy_memory_helper(dev_copy, MAX_COPY_DIMS - 1, dev_copy.src_begin, 0);
        } else {
            error(user_context) << "OpenGLCompute does not support 64-bit integers.\n";
            return -1;
        }
    } else if (buf->type.code == halide_type_uint) {
        if (buf->type.bits == 8) {
            converting_copy_memory_helper<uint8_t, float>(dev_copy, MAX_COPY_DIMS - 1, dev_copy.src_begin, 0);
        } else if (buf->type.bits == 16) {
            // Convert chunk_size in bytes to the number of items to be copied.
            // This doesn't happen for the 8-bit case because it would be a division by one,
            // and it doesn't happen for the 32-bit case as there is no data conversion and memcpy
            // is used.
            dev_copy.chunk_size /= 2;
            converting_copy_memory_helper<uint16_t, float>(dev_copy, MAX_COPY_DIMS - 1, dev_copy.src_begin, 0);
        } else if (buf->type.bits == 32) {
            copy_memory_helper(dev_copy, MAX_COPY_DIMS - 1, dev_copy.src_begin, 0);
        } else {
            error(user_context) << "OpenGLCompute does not support 64-bit integers.\n";
            return -1;
        }
    } else if (buf->type.code == halide_type_float) {
        if (buf->type.bits == 32) {
            copy_memory_helper(dev_copy, MAX_COPY_DIMS - 1, dev_copy.src_begin, 0);
        } else {
            error(user_context) << "OpenGLCompute does not support 64-bit floating-point.\n";
        }
    }
    global_state.UnmapBuffer(GL_ARRAY_BUFFER);

    debug(user_context) << "  copied " << ((unsigned)size) << " bytes from " << buf->host << " to the device.\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms for copy to dev\n";
#endif
    return 0;
}

// Copy image data from texture back to host memory.
WEAK int halide_openglcompute_copy_to_host(void *user_context, halide_buffer_t *buf) {
#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    if (!global_state.initialized) {
        error(user_context) << "OpenGL runtime not initialized (halide_openglcompute_copy_to_host).";
        return 1;
    }

    GLuint the_buffer = (GLuint)buf->device;
    size_t size = buf->size_in_bytes();
    halide_assert(user_context, size != 0);

    debug(user_context) << "OGLC: halide_openglcompute_copy_to_host ("
                        << "user_context: " << user_context
                        << ", buf: " << buf
                        << ", the_buffer:" << the_buffer
                        << ", size=" << (unsigned)size << ")\n";

    global_state.BindBuffer(GL_ARRAY_BUFFER, the_buffer);
    if (global_state.CheckAndReportError(user_context, "oglc: BindBuffer")) {
        return 1;
    }

    void *device_data = global_state.MapBufferRange(GL_ARRAY_BUFFER,
                                                    0,
                                                    size,
                                                    GL_MAP_READ_BIT);
    if (global_state.CheckAndReportError(user_context, "oglc: MapBufferRange")) {
        return 1;
    }

    halide_buffer_t buf_copy = *buf;
    buf_copy.device = (uint64_t)device_data;
    device_copy dev_copy = make_device_to_host_copy(&buf_copy);

    if (buf->type.code == halide_type_int) {
        if (buf->type.bits == 8) {
            converting_copy_memory_helper<float, int8_t>(dev_copy, MAX_COPY_DIMS - 1, 0, dev_copy.src_begin);
        } else if (buf->type.bits == 16) {
            // Convert chunk_size in bytes to the number of items to be copied.
            // This doesn't happen for the 8-bit case because it would be a division by one,
            // and it doesn't happen for the 32-bit case as there is no data conversion and memcpy
            // is used.
            dev_copy.chunk_size /= 2;
            converting_copy_memory_helper<float, int16_t>(dev_copy, MAX_COPY_DIMS - 1, 0, dev_copy.src_begin);
        } else if (buf->type.bits == 32) {
            copy_memory_helper(dev_copy, MAX_COPY_DIMS - 1, 0, dev_copy.src_begin);
        } else {
            error(user_context) << "OpenGLCompute does not support 64-bit integers.\n";
            return -1;
        }
    } else if (buf->type.code == halide_type_uint) {
        if (buf->type.bits == 8) {
            converting_copy_memory_helper<float, uint8_t>(dev_copy, MAX_COPY_DIMS - 1, 0, dev_copy.src_begin);
        } else if (buf->type.bits == 16) {
            // Convert chunk_size in bytes to the number of items to be copied.
            // This doesn't happen for the 8-bit case because it would be a division by one,
            // and it doesn't happen for the 32-bit case as there is no data conversion and memcpy
            // is used.
            dev_copy.chunk_size /= 2;
            converting_copy_memory_helper<float, uint16_t>(dev_copy, MAX_COPY_DIMS - 1, 0, dev_copy.src_begin);
        } else if (buf->type.bits == 32) {
            copy_memory_helper(dev_copy, MAX_COPY_DIMS - 1, 0, dev_copy.src_begin);
        } else {
            error(user_context) << "OpenGLCompute does not support 64-bit integers.\n";
            return -1;
        }
    } else if (buf->type.code == halide_type_float) {
        if (buf->type.bits == 32) {
            copy_memory_helper(dev_copy, MAX_COPY_DIMS - 1, 0, dev_copy.src_begin);
        } else {
            error(user_context) << "OpenGLCompute does not support 64-bit floating-point.\n";
        }
    }

    global_state.UnmapBuffer(GL_ARRAY_BUFFER);

    debug(user_context) << "  copied " << (unsigned)size << " bytes to the host.\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms for copy to host\n";
#endif

    return 0;
}

}  // namespace OpenGLCompute
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::Internal::OpenGLCompute;

//  Create wrappers that satisfy old naming conventions

extern "C" {

WEAK int halide_openglcompute_run(void *user_context, void *state_ptr,
                                  const char *entry_name, int blocksX, int blocksY,
                                  int blocksZ, int threadsX, int threadsY, int threadsZ,
                                  int shared_mem_bytes, halide_type_t arg_types[], void *args[],
                                  int8_t arg_is_buffer[], int num_attributes,
                                  float *vertex_buffer, int num_coords_dim0,
                                  int num_coords_dim1) {
#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    debug(user_context)
        << "OpenGLCompute: halide_openglcompute_run (user_context: " << user_context << ", "
        << "entry: " << entry_name << ", "
        << "blocks: " << blocksX << "x" << blocksY << "x" << blocksZ << ", "
        << "threads: " << threadsX << "x" << threadsY << "x" << threadsZ << ", "
        << "shmem: " << shared_mem_bytes << ", "
        << "num_attributes: " << num_attributes << ", "
        << "num_coords_dim0: " << num_coords_dim0 << ", "
        << "num_coords_dim1: " << num_coords_dim1 << "\n";

    if (!global_state.initialized) {
        error(user_context) << "OpenGL runtime not initialized (halide_openglcompute_run).";
        return -1;
    }

    ModuleState *mod = (ModuleState *)state_ptr;
    if (!mod) {
        error(user_context) << "Internal error: module state is NULL";
        return -1;
    }

    KernelInfo *kernel = find_kernel_by_name(entry_name, mod);
    if (!kernel) {
        error(user_context) << "Internal error: unknown kernel named '" << entry_name << "'";
        return -1;
    }

    global_state.UseProgram(kernel->program_id);
    if (global_state.CheckAndReportError(user_context, "halide_openglcompute_run UseProgram")) {
        return -1;
    }

    // Populate uniforms with values passed in arguments.
    // Order of the passed arguments matches what was generated for this kernel.
    int i = 0;
    while (arg_types[i].bits != 0) {
        debug(user_context) << "    args " << i
                            << " " << arg_types[i]
                            << " [" << (*((void **)args[i])) << " ...] "
                            << arg_is_buffer[i] << "\n";
        if (arg_is_buffer[i] == 0) {
            if (arg_types[i].code == halide_type_int) {
                int value;
                if (arg_types[i].bits == 8) {
                    value = *((int8_t *)args[i]);
                } else if (arg_types[i].bits == 16) {
                    value = *((int16_t *)args[i]);
                } else if (arg_types[i].bits == 32) {
                    value = *((int32_t *)args[i]);
                } else {
                    // error
                    return -1;
                }
                if (arg_types[i].bits <= 16) {
                    float fp_val = value;
                    global_state.Uniform1f(i, fp_val);
                    if (global_state.CheckAndReportError(user_context, "halide_openglcompute_run Uniform1f (int case)")) {
                        return -1;
                    }
                } else {
                    global_state.Uniform1i(i, value);
                    if (global_state.CheckAndReportError(user_context, "halide_openglcompute_run Uniform1i")) {
                        return -1;
                    }
                }
            } else if (arg_types[i].code == halide_type_uint) {
                unsigned value;
                if (arg_types[i].bits == 8) {
                    value = *((uint8_t *)args[i]);
                } else if (arg_types[i].bits == 16) {
                    value = *((uint16_t *)args[i]);
                } else if (arg_types[i].bits == 32) {
                    value = *((uint32_t *)args[i]);
                } else {
                    // error
                    return -1;
                }
                if (arg_types[i].bits <= 16) {
                    float fp_val = value;
                    global_state.Uniform1f(i, fp_val);
                    if (global_state.CheckAndReportError(user_context, "halide_openglcompute_run Uniform1f (uint case)")) {
                        return -1;
                    }
                } else {
                    global_state.Uniform1ui(i, value);
                    if (global_state.CheckAndReportError(user_context, "halide_openglcompute_run Uniform1ui")) {
                        return -1;
                    }
                }
            } else if (arg_types[i].code == halide_type_float) {
                float value;
                if (arg_types[i].bits == 32) {
                    value = *((float *)args[i]);
                } else {
                    // error
                    return -1;
                }
                global_state.Uniform1f(i, value);
                if (global_state.CheckAndReportError(user_context, "halide_openglcompute_run Uniform1f")) {
                    return -1;
                }
            } else {
                // error
                return -1;
            }
        } else {
            uint64_t arg_value = ((halide_buffer_t *)args[i])->device;

            GLuint the_buffer = (GLuint)arg_value;
            global_state.BindBufferBase(GL_SHADER_STORAGE_BUFFER, i, the_buffer);
            if (global_state.CheckAndReportError(user_context, "halide_openglcompute_run BindBufferBase")) {
                return -1;
            }
        }
        i++;
    }
    global_state.DispatchCompute(blocksX, blocksY, blocksZ);
    if (global_state.CheckAndReportError(user_context, "halide_openglcompute_run DispatchCompute")) {
        return -1;
    }
    global_state.MemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    if (global_state.CheckAndReportError(user_context, "halide_openglcompute_run MemoryBarrier")) {
        return -1;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms for run\n";
#endif

    return 0;
}

WEAK int halide_openglcompute_device_sync(void *user_context, halide_buffer_t *) {
#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    if (!global_state.initialized) {
        error(user_context) << "OpenGL Compute runtime not initialized (halide_openglcompute_device_sync).";
        return 1;
    }
    global_state.Finish();
#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms for sync\n";
#endif
    return 0;
}

namespace {
WEAK char *get_kernel_name(const char *start, const char *end) {
    const size_t kernel_name_length = end - start;
    char *kernel_name = (char *)malloc(kernel_name_length + 1);
    memcpy(kernel_name, start, kernel_name_length);
    kernel_name[kernel_name_length] = '\0';
    return kernel_name;
}
}  // namespace

// Called at the beginning of a code block generated by Halide. This function
// is responsible for setting up the OpenGL environment and compiling the GLSL
// code into a compute shader.
WEAK int halide_openglcompute_initialize_kernels(void *user_context, void **state_ptr,
                                                 const char *src, int size) {
#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

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

    if (module->kernel) {
        return 0;
    }

    const char *END_OF_KERNEL_MARKER = "\n// end of kernel ";
    const size_t END_OF_KERNEL_MARKER_LENGTH = strlen(END_OF_KERNEL_MARKER);

    while (1) {
        const char *end_of_kernel_marker = strstr(src, END_OF_KERNEL_MARKER);
        if (!end_of_kernel_marker) {
            break;  // end of kernels sources is reached
        }

        const char *just_before_kernel_name = end_of_kernel_marker + END_OF_KERNEL_MARKER_LENGTH;
        const char *just_beyond_kernel_name = strstr(just_before_kernel_name, "\n");
        if (!just_beyond_kernel_name) {
            error(user_context) << "Failed to find kernel name.\n";
        }

        char *kernel_name = get_kernel_name(just_before_kernel_name, just_beyond_kernel_name);

        size_t src_len = just_beyond_kernel_name - src;

        KernelInfo *kernel = (KernelInfo *)malloc(sizeof(KernelInfo));
        kernel->kernel_name = kernel_name;
        kernel->next = module->kernel;
        module->kernel = kernel;

        GLuint shader = global_state.CreateShader(GL_COMPUTE_SHADER);
        if (global_state.CheckAndReportError(user_context, "create shader")) {
            return -1;
        }
        const GLchar *sources = {src};
        const GLint sources_lengths = {(GLint)src_len};

#ifdef DEBUG_RUNTIME
        print(user_context) << "Compute shader source for: " << kernel_name;
        halide_print(user_context, src);
#endif

        global_state.ShaderSource(shader, 1, &sources, &sources_lengths);
        if (global_state.CheckAndReportError(user_context, "shader source")) {
            return -1;
        }
        global_state.CompileShader(shader);
        if (global_state.CheckAndReportError(user_context, "compile shader")) {
            return -1;
        }

        GLint shader_ok = 0;
        global_state.GetShaderiv(shader, GL_COMPILE_STATUS, &shader_ok);
        if (shader_ok != GL_TRUE) {
            print(user_context) << "Could not compile shader:\n";
            GLint log_len;
            global_state.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
            HalideMalloc log_tmp(user_context, log_len);
            if (log_tmp.ptr) {
                char *log = (char *)log_tmp.ptr;
                global_state.GetShaderInfoLog(shader, log_len, NULL, log);
                print(user_context) << log << "\n";
            }
            global_state.DeleteShader(shader);
            return -1;
        }

        // Link GLSL program
        GLuint program = global_state.CreateProgram();
        global_state.AttachShader(program, shader);
        if (global_state.CheckAndReportError(user_context, "attach shader")) {
            return -1;
        }
        global_state.LinkProgram(program);
        if (global_state.CheckAndReportError(user_context, "link program")) {
            return -1;
        }

        // Release the individual shaders
        global_state.DeleteShader(shader);

        GLint status;
        global_state.GetProgramiv(program, GL_LINK_STATUS, &status);
        if (!status) {
            GLint log_len;
            global_state.GetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
            HalideMalloc log_tmp(user_context, log_len);
            if (log_tmp.ptr) {
                char *log = (char *)log_tmp.ptr;
                global_state.GetProgramInfoLog(program, log_len, NULL, log);
                debug(user_context) << "Could not link GLSL program:\n"
                                    << log << "\n";
            }
            global_state.DeleteProgram(program);
            return -1;
        }
        kernel->program_id = program;

#ifdef DEBUG_RUNTIME
        GLint i;
        GLint count;

        GLint size;   // size of the variable
        GLenum type;  // type of the variable (float, vec3 or mat4, etc)

        const GLsizei bufSize = 64;  // maximum name length
        GLchar name[bufSize];        // variable name in GLSL
        GLsizei length;              // name length

        global_state.GetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
        debug(user_context) << "Active Uniforms: " << count << "\n";

        for (i = 0; i < count; i++) {
            global_state.GetActiveUniform(program, (GLuint)i, bufSize, &length, &size, &type, name);
            GLint loc = global_state.GetUniformLocation(program, name);
            debug(user_context) << "Uniform " << i << " Type: " << type << " Name: " << name << " location: " << loc << "\n";
        }
#endif
        src += src_len;  // moving on to the next kernel
    }
#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6
                        << " ms\n";
#endif

    return 0;
}

WEAK int halide_openglcompute_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf) {
    return halide_default_device_and_host_malloc(user_context, buf, &openglcompute_device_interface);
}

WEAK int halide_openglcompute_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    return halide_default_device_and_host_free(user_context, buf, &openglcompute_device_interface);
}

WEAK const struct halide_device_interface_t *halide_openglcompute_device_interface() {
    return &openglcompute_device_interface;
}

}  // extern "C"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace OpenGLCompute {

WEAK halide_device_interface_impl_t openglcompute_device_interface_impl = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_openglcompute_device_malloc,
    halide_openglcompute_device_free,
    halide_openglcompute_device_sync,
    halide_openglcompute_device_release,
    halide_openglcompute_copy_to_host,
    halide_openglcompute_copy_to_device,
    halide_openglcompute_device_and_host_malloc,
    halide_openglcompute_device_and_host_free,
    halide_default_buffer_copy,
    halide_default_device_crop,
    halide_default_device_slice,
    halide_default_device_release_crop,
    halide_default_device_wrap_native,
    halide_default_device_detach_native,
};

WEAK halide_device_interface_t openglcompute_device_interface = {
    halide_device_malloc,
    halide_device_free,
    halide_device_sync,
    halide_device_release,
    halide_copy_to_host,
    halide_copy_to_device,
    halide_device_and_host_malloc,
    halide_device_and_host_free,
    halide_buffer_copy,
    halide_device_crop,
    halide_device_slice,
    halide_device_release_crop,
    halide_device_wrap_native,
    halide_device_detach_native,
    NULL,
    &openglcompute_device_interface_impl};

}  // namespace OpenGLCompute
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
