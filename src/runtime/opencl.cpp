#include "HalideRuntimeOpenCL.h"
#include "scoped_spin_lock.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"

#include "mini_cl.h"
#include "hashmap.h"

#define INLINE inline __attribute__((always_inline))

namespace Halide { namespace Runtime { namespace Internal { namespace OpenCL {

/// Borrowed from write_debug_image.cpp. Is there good place to move definition?
struct ScopedFile {
    void *f;
    ScopedFile(const char *filename, const char *mode) {
        f = fopen(filename, mode);
    }
    ~ScopedFile() {
        if (open()) {
            fclose(f);
        }
    }
    bool write(const void *ptr, size_t bytes) {
        return fwrite(ptr, bytes, 1, f);
    }

    bool readall(unsigned char **dataptr, size_t *sizeptr) {
        /* None of the parameters can be NULL. */
        if (!open() || dataptr == NULL || sizeptr == NULL) {
            return false;
        }
        unsigned char *data = NULL;
        unsigned char *temp = NULL;

        const size_t CHUNK_SIZE = 2048;

        size_t size = 0;
        size_t used = 0;

        /* A read error already occurred? */
        if (ferror(f)) {
            return false;
        }

        while (true) {
            if (used + CHUNK_SIZE + 1 > size) {
                size = used + CHUNK_SIZE + 1;
                /* Overflow check. */
                if (size <= used) {
                    free(data);
                    return false;
                }
                temp = (unsigned char*)realloc(data, size);
                if (temp == NULL) {
                    free(data);
                    return false;
                }
                data = temp;
            }
            debug(0) << "    used: " << (uint32_t)used << "\n";
            size_t n = fread(data + used, 1, CHUNK_SIZE, f);
            debug(0) << "    fread.n: " << (uint32_t)n << "\n";
            if (n == 0) {
                break;
            }

            used += n;
        }

        if (ferror(f)) {
            free(data);
            return false;
        }

        temp = (unsigned char*)realloc(data, used + 1);
        if (temp == NULL) {
            free(data);
            return false;
        }
        data = temp;
        data[used] = '\0';
        debug(0) << (char*)data << "\n\n\n";

        *dataptr = data;
        *sizeptr = used;

        return true;
    }

    bool open() const {
        return f;
    }
};

// Define the function pointers for the OpenCL API. OpenCL 1.2
// currently disabled so we can work on build bots without it.
//#define HAVE_OPENCL_12
#define CL_FN(ret, fn, args) WEAK ret (CL_API_CALL *fn) args;
#include "cl_functions.h"

// The default implementation of halide_opencl_get_symbol attempts to load
// the OpenCL runtime shared library/DLL, and then get the symbol from it.
WEAK void *lib_opencl = NULL;

extern "C" WEAK void *halide_opencl_get_symbol(void *user_context, const char *name) {
    // Only try to load the library if the library isn't already
    // loaded, or we can't load the symbol from the process already.
    void *symbol = halide_get_library_symbol(lib_opencl, name);
    if (symbol) {
        return symbol;
    }

    const char *lib_names[] = {
#ifdef WINDOWS
        "opencl.dll",
#else
        "libOpenCL.so",
        "/System/Library/Frameworks/OpenCL.framework/OpenCL",
#endif
    };
    for (size_t i = 0; i < sizeof(lib_names)/sizeof(lib_names[0]); i++) {
        lib_opencl = halide_load_library(lib_names[i]);
        if (lib_opencl) {
            debug(user_context) << "    Loaded OpenCL runtime library: " << lib_names[i] << "\n";
            break;
        }
    }

    return halide_get_library_symbol(lib_opencl, name);
}

template <typename T>
INLINE T get_cl_symbol(void *user_context, const char *name) {
    T s = (T)halide_opencl_get_symbol(user_context, name);
    if (!s) {
        error(user_context) << "OpenCL API not found: " << name << "\n";
    }
    return s;
}

// Load an OpenCL shared object/dll, and get the function pointers for the OpenCL API from it.
WEAK void load_libopencl(void *user_context) {
    debug(user_context) << "    load_libopencl (user_context: " << user_context << ")\n";
    halide_assert(user_context, clCreateContext == NULL);

    #define CL_FN(ret, fn, args) fn = get_cl_symbol<ret (CL_API_CALL *)args>(user_context, #fn);
    #include "cl_functions.h"
}

extern WEAK halide_device_interface_t opencl_device_interface;

WEAK const char *get_opencl_error_name(cl_int err);
WEAK int create_opencl_context(void *user_context, cl_context *ctx, cl_command_queue *q);

// An OpenCL context/queue/synchronization lock defined in
// this module with weak linkage
cl_context WEAK context = 0;
cl_command_queue WEAK command_queue = 0;
volatile int WEAK thread_lock = 0;

WEAK char platform_name[256];
WEAK int platform_name_lock = 0;
WEAK bool platform_name_initialized = false;

WEAK char device_type[256];
WEAK int device_type_lock = 0;
WEAK bool device_type_initialized = false;

WEAK char device_built_programs_cache_dir_name[1024];
WEAK bool device_built_programs_cache_dir_name_set = false;

}}}} // namespace Halide::Runtime::Internal::OpenCL

using namespace Halide::Runtime::Internal::OpenCL;

// Allow OpenCL 1.1 features to be used.
#define ENABLE_OPENCL_11

extern "C" {
WEAK extern void halide_opencl_set_compiled_programs_cache_dir(const char *path) {
    debug(0) << "halide_opencl_set_compiled_programs_cache_dir: \n";
    if (path) {
        strncpy(device_built_programs_cache_dir_name, path, 1024);
        device_built_programs_cache_dir_name_set = true;
        debug(0) << "    device_built_programs_cache_dir_name_set: " << device_built_programs_cache_dir_name_set << "\n";
    } else {
        device_built_programs_cache_dir_name[0] = 0;
    }
}

WEAK void halide_opencl_set_platform_name(const char *n) {
    if (n) {
        strncpy(platform_name, n, 255);
    } else {
        platform_name[0] = 0;
    }
    platform_name_initialized = true;
}

WEAK const char *halide_opencl_get_platform_name(void *user_context) {
    ScopedSpinLock lock(&platform_name_lock);
    if (!platform_name_initialized) {
        const char *name = getenv("HL_OCL_PLATFORM_NAME");
        halide_opencl_set_platform_name(name);
    }
    return platform_name;
}


WEAK void halide_opencl_set_device_type(const char *n) {
    if (n) {
        strncpy(device_type, n, 255);
    } else {
        device_type[0] = 0;
    }
    device_type_initialized = true;
}

WEAK const char *halide_opencl_get_device_type(void *user_context) {
    ScopedSpinLock lock(&device_type_lock);
    if (!device_type_initialized) {
        const char *name = getenv("HL_OCL_DEVICE_TYPE");
        halide_opencl_set_device_type(name);
    }
    return device_type;
}

// The default implementation of halide_acquire_cl_context uses the global
// pointers above, and serializes access with a spin lock.
// Overriding implementations of acquire/release must implement the following
// behavior:
// - halide_acquire_cl_context should always store a valid context/command
//   queue in ctx/q, or return an error code.
// - A call to halide_acquire_cl_context is followed by a matching call to
//   halide_release_cl_context. halide_acquire_cl_context should block while a
//   previous call (if any) has not yet been released via halide_release_cl_context.
WEAK int halide_acquire_cl_context(void *user_context, cl_context *ctx, cl_command_queue *q, bool create = true) {
    // TODO: Should we use a more "assertive" assert? These asserts do
    // not block execution on failure.
    halide_assert(user_context, ctx != NULL);
    halide_assert(user_context, q != NULL);

    halide_assert(user_context, &thread_lock != NULL);
    while (__sync_lock_test_and_set(&thread_lock, 1)) { }

    // If the context has not been initialized, initialize it now.
    halide_assert(user_context, &context != NULL);
    halide_assert(user_context, &command_queue != NULL);
    if (!context && create) {
        cl_int error = create_opencl_context(user_context, &context, &command_queue);
        if (error != CL_SUCCESS) {
            __sync_lock_release(&thread_lock);
            return error;
        }
    }

    *ctx = context;
    *q = command_queue;
    return 0;
}

WEAK int halide_release_cl_context(void *user_context) {
    __sync_lock_release(&thread_lock);
    return 0;
}

} // extern "C"

namespace Halide { namespace Runtime { namespace Internal { namespace OpenCL {

// Helper object to acquire and release the OpenCL context.
class ClContext {
    void *user_context;

public:
    cl_context context;
    cl_command_queue cmd_queue;
    cl_int error;

    // Constructor sets 'error' if any occurs.
    INLINE ClContext(void *user_context) : user_context(user_context),
                                    context(NULL),
                                    cmd_queue(NULL),
                                    error(CL_SUCCESS) {
        if (clCreateContext == NULL) {
            load_libopencl(user_context);
        }

#ifdef DEBUG_RUNTIME
        halide_start_clock(user_context);
#endif

        error = halide_acquire_cl_context(user_context, &context, &cmd_queue);
        halide_assert(user_context, context != NULL && cmd_queue != NULL);
    }

    INLINE ~ClContext() {
        halide_release_cl_context(user_context);
    }
};

// OpenCL doesn't support creating sub-buffers from some-buffers.  In
// order to support more generalized (and frankly, minimally useful)
// crop behavior, we store a cl_mem and an offset and then create
// sub-buffers as needed.
struct device_handle {
    // Important: order these to avoid any padding between fields;
    // some Win32 compiler optimizer configurations can inconsistently
    // insert padding otherwise.
    uint64_t offset;
    cl_mem mem;
};

// Structure to hold the state of a module attached to the context.
// Also used as a linked-list to keep track of all the different
// modules that are attached to a context in order to release them all
// when then context is released.
struct module_state {
    cl_program program;
    module_state *next;
};
WEAK module_state *state_list = NULL;

WEAK bool validate_device_pointer(void *user_context, halide_buffer_t* buf, size_t size=0) {
    if (buf->device == 0) {
        return true;
    }

    // We may call this in situations where we haven't loaded the
    // OpenCL API yet.
    if (!clGetMemObjectInfo) {
        load_libopencl(user_context);
    }

    cl_mem dev_ptr = ((device_handle *)buf->device)->mem;
    uint64_t offset = ((device_handle *)buf->device)->offset;

    size_t real_size;
    cl_int result = clGetMemObjectInfo(dev_ptr, CL_MEM_SIZE, sizeof(size_t), &real_size, NULL);
    if (result != CL_SUCCESS) {
        error(user_context) << "CL: Bad device pointer " << (void *)dev_ptr
                            << ": clGetMemObjectInfo returned "
                            << get_opencl_error_name(result);
        return false;
    }

    debug(user_context) << "CL: validate " << (void *)dev_ptr << " offset: " << offset
                        << ": asked for " << (uint64_t)size
                        << ", actual allocated " << (uint64_t)real_size << "\n";

    if (size) {
        halide_assert(user_context, real_size >= (size + offset) && "Validating pointer with insufficient size");
    }
    return true;
}

// Initializes the context used by the default implementation
// of halide_acquire_context.
WEAK int create_opencl_context(void *user_context, cl_context *ctx, cl_command_queue *q) {
    debug(user_context)
        << "    create_opencl_context (user_context: " << user_context << ")\n";

    halide_assert(user_context, ctx != NULL && *ctx == NULL);
    halide_assert(user_context, q != NULL && *q == NULL);

    cl_int err = 0;

    const cl_uint max_platforms = 4;
    cl_platform_id platforms[max_platforms];
    cl_uint platform_count = 0;

    err = clGetPlatformIDs(max_platforms, platforms, &platform_count);
    if (err != CL_SUCCESS) {
        error(user_context) << "CL: clGetPlatformIDs failed: "
                            << get_opencl_error_name(err) << " " << err;
        return err;
    }

    cl_platform_id platform = NULL;

    // Find the requested platform, or the first if none specified.
    const char *name = halide_opencl_get_platform_name(user_context);
    if (name != NULL) {
        for (cl_uint i = 0; i < platform_count; ++i) {
            const cl_uint max_platform_name = 256;
            char platform_name[max_platform_name];
            err = clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, max_platform_name, platform_name, NULL );
            if (err != CL_SUCCESS) continue;
            debug(user_context) << "CL: platform " << i << " " << platform_name << "\n";

            // A platform matches the request if it is a substring of the platform name.
            if (strstr(platform_name, name)) {
                platform = platforms[i];
                break;
            }
        }
    } else if (platform_count > 0) {
        platform = platforms[0];
    }
    if (platform == NULL){
        error(user_context) << "CL: Failed to find platform\n";
        return CL_INVALID_PLATFORM;
    }

    #ifdef DEBUG_RUNTIME
    const cl_uint max_platform_name = 256;
    char platform_name[max_platform_name];
    err = clGetPlatformInfo(platform, CL_PLATFORM_NAME, max_platform_name, platform_name, NULL );
    if (err != CL_SUCCESS) {
        debug(user_context) << "    clGetPlatformInfo(CL_PLATFORM_NAME) failed: "
                            << get_opencl_error_name(err) << "\n";
        // This is just debug info, report the error but don't fail context creation due to it.
        //return err;
    } else {
        debug(user_context) << "    Got platform '" << platform_name
                            << "', about to create context (t="
                            << halide_current_time_ns(user_context)
                            << ")\n";
    }
    #endif

    // Get the types of devices requested.
    cl_device_type device_type = 0;
    const char * dev_type = halide_opencl_get_device_type(user_context);
    if (dev_type != NULL && *dev_type != '\0') {
        if (strstr(dev_type, "cpu")) {
            device_type |= CL_DEVICE_TYPE_CPU;
        }
        if (strstr(dev_type, "gpu")) {
            device_type |= CL_DEVICE_TYPE_GPU;
        }
        if (strstr(dev_type, "acc")) {
            device_type |= CL_DEVICE_TYPE_ACCELERATOR;
        }
    }
    // If no device types are specified, use all the available
    // devices.
    if (device_type == 0) {
        device_type = CL_DEVICE_TYPE_ALL;
    }

    // Get all the devices of the specified type.
    const cl_uint maxDevices = 4;
    cl_device_id devices[maxDevices];
    cl_uint deviceCount = 0;
    err = clGetDeviceIDs(platform, device_type, maxDevices, devices, &deviceCount );
    if (err != CL_SUCCESS) {
        error(user_context) << "CL: clGetDeviceIDs failed: "
                            << get_opencl_error_name(err);
        return err;
    }

    // If the user indicated a specific device index to use, use
    // that. Note that this is an index within the set of devices
    // specified by the device type. -1 means select a device
    // automatically based on core count.
    int device = halide_get_gpu_device(user_context);
    if (device == -1 && deviceCount == 1) {
        device = 0;
    } else if (device == -1) {
        debug(user_context) << "    Multiple CL devices detected. Selecting the one with the most cores.\n";
        cl_uint best_core_count = 0;
        for (cl_uint i = 0; i < deviceCount; i++) {
            cl_device_id dev = devices[i];
            cl_uint core_count = 0;
            err = clGetDeviceInfo(dev, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cl_uint), &core_count, NULL);
            if (err != CL_SUCCESS) {
                debug(user_context) << "      Failed to get info on device " << i << "\n";
                continue;
            }
            debug(user_context) << "      Device " << i << " has " << core_count << " cores\n";
            if (core_count >= best_core_count) {
                device = i;
                best_core_count = core_count;
            }
        }
        debug(user_context) << "    Selected device " << device << "\n";
    }

    if (device < 0 || device >= (int)deviceCount) {
        error(user_context) << "CL: Failed to get device: " << device;
        return CL_DEVICE_NOT_FOUND;
    }

    cl_device_id dev = devices[device];

    #ifdef DEBUG_RUNTIME
    // Declare variables for other state we want to query.
    char device_name[256] = "";
    char device_vendor[256] = "";
    char device_profile[256] = "";
    char device_version[256] = "";
    char driver_version[256] = "";
    cl_ulong global_mem_size = 0;
    cl_ulong max_mem_alloc_size = 0;
    cl_ulong local_mem_size = 0;
    cl_uint max_compute_units = 0;
    size_t max_work_group_size = 0;
    cl_uint max_work_item_dimensions = 0;
    size_t max_work_item_sizes[4] = { 0, };


    struct {void *dst; size_t sz; cl_device_info param;} infos[] = {
        {&device_name[0], sizeof(device_name), CL_DEVICE_NAME},
        {&device_vendor[0], sizeof(device_vendor), CL_DEVICE_VENDOR},
        {&device_profile[0], sizeof(device_profile), CL_DEVICE_PROFILE},
        {&device_version[0], sizeof(device_version), CL_DEVICE_VERSION},
        {&driver_version[0], sizeof(driver_version), CL_DRIVER_VERSION},
        {&global_mem_size, sizeof(global_mem_size), CL_DEVICE_GLOBAL_MEM_SIZE},
        {&max_mem_alloc_size, sizeof(max_mem_alloc_size), CL_DEVICE_MAX_MEM_ALLOC_SIZE},
        {&local_mem_size, sizeof(local_mem_size), CL_DEVICE_LOCAL_MEM_SIZE},
        {&max_compute_units, sizeof(max_compute_units), CL_DEVICE_MAX_COMPUTE_UNITS},
        {&max_work_group_size, sizeof(max_work_group_size), CL_DEVICE_MAX_WORK_GROUP_SIZE},
        {&max_work_item_dimensions, sizeof(max_work_item_dimensions), CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS},
        {&max_work_item_sizes[0], sizeof(max_work_item_sizes), CL_DEVICE_MAX_WORK_ITEM_SIZES},
        {NULL}};

    // Do all the queries.
    for (int i = 0; infos[i].dst; i++) {
        err = clGetDeviceInfo(dev, infos[i].param, infos[i].sz, infos[i].dst, NULL);
        if (err != CL_SUCCESS) {
            error(user_context) << "CL: clGetDeviceInfo failed: "
                                << get_opencl_error_name(err);
            return err;
        }
    }

    debug(user_context)
        << "      device name: " << device_name << "\n"
        << "      device vendor: " << device_vendor << "\n"
        << "      device profile: " << device_profile << "\n"
        << "      global mem size: " << global_mem_size/(1024*1024) << " MB\n"
        << "      max mem alloc size: " << max_mem_alloc_size/(1024*1024) << " MB\n"
        << "      local mem size: " << local_mem_size << "\n"
        << "      max compute units: " << max_compute_units << "\n"
        << "      max workgroup size: " << (uint64_t)max_work_group_size << "\n"
        << "      max work item dimensions: " << max_work_item_dimensions << "\n"
        << "      max work item sizes: " << (uint64_t)max_work_item_sizes[0]
        << "x" << (uint64_t)max_work_item_sizes[1]
        << "x" << (uint64_t)max_work_item_sizes[2]
        << "x" << (uint64_t)max_work_item_sizes[3] << "\n";
    #endif


    // Create context and command queue.
    cl_context_properties properties[] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0 };
    debug(user_context) << "    clCreateContext -> ";
    *ctx = clCreateContext(properties, 1, &dev, NULL, NULL, &err);
    if (err != CL_SUCCESS) {
        debug(user_context) << get_opencl_error_name(err);
        error(user_context) << "CL: clCreateContext failed: "
                            << get_opencl_error_name(err)
                            << ":" << (int)err;
        return err;
    } else {
        debug(user_context) << *ctx << "\n";
    }

    debug(user_context) << "    clCreateCommandQueue ";
    *q = clCreateCommandQueue(*ctx, dev, 0, &err);
    if (err != CL_SUCCESS) {
        debug(user_context) << get_opencl_error_name(err);
        error(user_context) << "CL: clCreateCommandQueue failed: "
                            << get_opencl_error_name(err);
        return err;
    } else {
        debug(user_context) << *q << "\n";
    }

    return err;
}

char *make_cached_program_filename(const char * const device_built_programs_cache_dir_name, const uint32_t src_hash) {
    debug(0) << "make_cached_program_filename: " << device_built_programs_cache_dir_name << " " << src_hash << "\n";
    const size_t BUF_SIZE = 1024; // Max path size

    char *buf = (char*)malloc(BUF_SIZE);
    if (buf == NULL) {
        return NULL;
    }

    const int n = sprintf(buf, "%s/halide_opencl_program_binary_%x.bin", device_built_programs_cache_dir_name, src_hash);
    if (n > 0) {
        debug(0) << "    path: " << buf << "\n";
        return buf;
    }
    free(buf);
    return NULL;
}

}}}} // namespace Halide::Runtime::Internal::OpenCL

extern "C" {

WEAK int halide_opencl_device_free(void *user_context, halide_buffer_t* buf) {
    // halide_opencl_device_free, at present, can be exposed to clients and they
    // should be allowed to call halide_opencl_device_free on any halide_buffer_t
    // including ones that have never been used with a GPU.
    if (buf->device == 0) {
        return 0;
    }

    cl_mem dev_ptr = ((device_handle *)buf->device)->mem;
    halide_assert(user_context, (((device_handle *)buf->device)->offset == 0) && "halide_opencl_device_free on buffer obtained from halide_device_crop");

    debug(user_context)
        << "CL: halide_opencl_device_free (user_context: " << user_context
        << ", buf: " << buf << ") cl_mem: " << dev_ptr << "\n";

    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, validate_device_pointer(user_context, buf));
    debug(user_context) << "    clReleaseMemObject " << (void *)dev_ptr << "\n";
    cl_int result = clReleaseMemObject((cl_mem)dev_ptr);
    // If clReleaseMemObject fails, it is unlikely to succeed in a later call, so
    // we just end our reference to it regardless.
    free((device_handle *)buf->device);
    buf->device = 0;
    buf->device_interface->impl->release_module();
    buf->device_interface = NULL;
    if (result != CL_SUCCESS) {
        // We may be called as a destructor, so don't raise an error
        // here.
        return result;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}


WEAK int halide_opencl_initialize_kernels(void *user_context, void **state_ptr, const char* src, int size) {
    debug(user_context)
        << "CL: halide_opencl_init_kernels (user_context: " << user_context
        << ", state_ptr: " << state_ptr
        << ", program: " << (void *)src
        << ", size: " << size << "\n";

    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    // Create the state object if necessary. This only happens once, regardless
    // of how many times halide_init_kernels/halide_release is called.
    // halide_release traverses this list and releases the program objects, but
    // it does not modify the list nodes created/inserted here.
    module_state **state = (module_state**)state_ptr;
    if (!(*state)) {
        *state = (module_state*)malloc(sizeof(module_state));
        (*state)->program = NULL;
        (*state)->next = state_list;
        state_list = *state;
    }

    // Create the program if necessary. TODO: The program object needs to not
    // only already exist, but be created for the same context/device as the
    // calling context/device.
    if (!(*state && (*state)->program) && size > 1) {
        cl_int err = 0;
        cl_device_id dev;

        err = clGetContextInfo(ctx.context, CL_CONTEXT_DEVICES, sizeof(dev), &dev, NULL);
        if (err != CL_SUCCESS) {
            error(user_context) << "CL: clGetContextInfo(CL_CONTEXT_DEVICES) failed: "
                                << get_opencl_error_name(err);
            return err;
        }

        cl_device_id devices[] = { dev };

        // Get the max constant buffer size supported by this OpenCL implementation.
        cl_ulong max_constant_buffer_size = 0;
        err = clGetDeviceInfo(dev, CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE, sizeof(max_constant_buffer_size), &max_constant_buffer_size, NULL);
        if (err != CL_SUCCESS) {
            error(user_context) << "CL: clGetDeviceInfo (CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE) failed: "
                                << get_opencl_error_name(err);
            return err;
        }
        // Get the max number of constant arguments supported by this OpenCL implementation.
        cl_uint max_constant_args = 0;
        err = clGetDeviceInfo(dev, CL_DEVICE_MAX_CONSTANT_ARGS, sizeof(max_constant_args), &max_constant_args, NULL);
        if (err != CL_SUCCESS) {
            error(user_context) << "CL: clGetDeviceInfo (CL_DEVICE_MAX_CONSTANT_ARGS) failed: "
                                << get_opencl_error_name(err);
            return err;
        }

        // Check if program was compiled before, then load it from cache.
        // @TODO It's possible to calculate src_hash at compile-time in CodeGen_GPU_Host<CodeGen_CPU>::compile_funcs
        debug(0) << "calculate hash: \n";
        uint32_t src_hash = device_built_programs_cache_dir_name_set ? Halide::Runtime::Internal::djb_hash((const uint8_t *)src, size) : 0;
        debug(0) << "    src_hash: " << src_hash << "\n";
        cl_program program = NULL;
        if (device_built_programs_cache_dir_name_set) {
            char *cached_program_filename = make_cached_program_filename(device_built_programs_cache_dir_name, src_hash);
            if (cached_program_filename != NULL) {
                debug(0) << "    open file\n";
                ScopedFile file(cached_program_filename, "rb");
                unsigned char *binary_buffer = NULL;
                size_t binary_size = 0;
                if (file.readall(&binary_buffer, &binary_size)) {
                    debug(0) << "    binary_size: " << (uint32_t)binary_size << "\n";
                    debug(0) << "    binary: " << (char*)binary_buffer;
                    debug(0) << "\n";
                    const unsigned char *binaries[] = { binary_buffer };
                    cl_int load_errors[1];
                    cl_program program_from_binary = clCreateProgramWithBinary(ctx.context, 1, devices, &binary_size, binaries, load_errors, &err);
                    if (err != CL_SUCCESS) {
                        debug(user_context) << get_opencl_error_name(err) << "\n";
                        error(user_context) << "CL: clCreateProgramWithBinary failed: "
                                            << get_opencl_error_name(err);
                        return err;
                    } else {
                        program = program_from_binary;
                        debug(user_context) << "    program was loaded from binaries cache file " << cached_program_filename << ": "  << (void *) program << "\n";
                    }
                    free(binary_buffer);

                    stringstream options(user_context);
                    options << "-D MAX_CONSTANT_BUFFER_SIZE=" << max_constant_buffer_size
                            << " -D MAX_CONSTANT_ARGS=" << max_constant_args;

                    debug(user_context) << "    clBuildProgram " << (void *)program
                                        << " " << options.str() << "\n";
                    err = clBuildProgram(program, 1, devices, options.str(), NULL, NULL );
                }
                free(cached_program_filename);
                debug(0) << "    read done\n";
            }
        }

        debug(0) << "    program == NULL: " << (program == NULL) << "\n";
        if (program == NULL) {
            // Build the compile argument options.
            const char *sources[] = {src};
            debug(user_context) << "    clCreateProgramWithSource -> ";
            program = clCreateProgramWithSource(ctx.context, 1, &sources[0], NULL, &err);
            if (err != CL_SUCCESS) {
                debug(user_context) << get_opencl_error_name(err) << "\n";
                error(user_context) << "CL: clCreateProgramWithSource failed: "
                                    << get_opencl_error_name(err);
                return err;
            } else {
                debug(user_context) << (void *) program << "\n";
            }

            stringstream options(user_context);
            options << "-D MAX_CONSTANT_BUFFER_SIZE=" << max_constant_buffer_size
                    << " -D MAX_CONSTANT_ARGS=" << max_constant_args;

            debug(user_context) << "    clBuildProgram " << (void *)program
                                << " " << options.str() << "\n";
            err = clBuildProgram(program, 1, devices, options.str(), NULL, NULL );

            if (err != CL_SUCCESS) {
                // Allocate an appropriately sized buffer for the build log.
                char buffer[8192];

                // Get build log
                if (clGetProgramBuildInfo(program, dev,
                                          CL_PROGRAM_BUILD_LOG,
                                          sizeof(buffer), buffer,
                                          NULL) == CL_SUCCESS) {
                    error(user_context) << "CL: clBuildProgram failed: "
                                        << get_opencl_error_name(err)
                                        << "\nBuild Log:\n"
                                        << buffer << "\n";
                } else {
                    error(user_context) << "clGetProgramBuildInfo failed";
                }

                return err;
            }
        }

        if (program == NULL) {
            error(user_context) << "CL: halide_opencl_initialize_kernels failed to compile program and load it from cache.";
            return CL_BUILD_ERROR;
        }
        (*state)->program = program;

        if (device_built_programs_cache_dir_name_set) {
            size_t bin_size;
            size_t ret_size;
            //device_built_programs_cache_dir_name
            err = clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES, sizeof(size_t), &bin_size, &ret_size);
            if (err == CL_SUCCESS && bin_size) {
                // I don't like manual memory handling. Why don't use shared_ptr?
                char *cached_program_filename = make_cached_program_filename(device_built_programs_cache_dir_name, src_hash);
                ScopedFile file(cached_program_filename, "wb");
                free(cached_program_filename);

                if (file.open()) {
                    unsigned char *bin_buf = (unsigned char*)malloc(bin_size + 128);
                    err = clGetProgramInfo(program, CL_PROGRAM_BINARIES, bin_size, &bin_buf, &ret_size);
                    if (err == CL_SUCCESS) {
                        file.write(bin_buf, bin_size);
                    }
                    free(bin_buf);
                }
            }
        }
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

// Used to generate correct timings when tracing
WEAK int halide_opencl_device_sync(void *user_context, halide_buffer_t *) {
    debug(user_context) << "CL: halide_opencl_device_sync (user_context: " << user_context << ")\n";

    ClContext ctx(user_context);
    halide_assert(user_context, ctx.error == CL_SUCCESS);

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    cl_int err = clFinish(ctx.cmd_queue);
    if (err != CL_SUCCESS) {
        error(user_context) << "CL: clFinish failed: "
                            << get_opencl_error_name(err);
        return err;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return CL_SUCCESS;
}

WEAK int halide_opencl_device_release(void *user_context) {
    debug(user_context)
        << "CL: halide_opencl_device_release (user_context: " << user_context << ")\n";

    // The ClContext object does not allow the context storage to be modified,
    // so we use halide_acquire_context directly.
    int err;
    cl_context ctx;
    cl_command_queue q;
    err = halide_acquire_cl_context(user_context, &ctx, &q, false);
    if (err != 0) {
        return err;
    }

    if (ctx) {
        err = clFinish(q);
        halide_assert(user_context, err == CL_SUCCESS);

        // Unload the modules attached to this context. Note that the list
        // nodes themselves are not freed, only the program objects are
        // released. Subsequent calls to halide_init_kernels might re-create
        // the program object using the same list node to store the program
        // object.
        module_state *state = state_list;
        while (state) {
            if (state->program) {
                debug(user_context) << "    clReleaseProgram " << state->program << "\n";
                err = clReleaseProgram(state->program);
                halide_assert(user_context, err == CL_SUCCESS);
                state->program = NULL;
            }
            state = state->next;
        }

        // Release the context itself, if we created it.
        if (ctx == context) {
            debug(user_context) << "    clReleaseCommandQueue " << command_queue << "\n";
            err = clReleaseCommandQueue(command_queue);
            halide_assert(user_context, err == CL_SUCCESS);
            command_queue = NULL;

            debug(user_context) << "    clReleaseContext " << context << "\n";
            err = clReleaseContext(context);
            halide_assert(user_context, err == CL_SUCCESS);
            context = NULL;
        }
    }

    halide_release_cl_context(user_context);

    return 0;
}

WEAK int halide_opencl_device_malloc(void *user_context, halide_buffer_t* buf) {
    debug(user_context)
        << "CL: halide_opencl_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    size_t size = buf->size_in_bytes();
    halide_assert(user_context, size != 0);
    if (buf->device) {
        halide_assert(user_context, validate_device_pointer(user_context, buf, size));
        return 0;
    }

    for (int i = 0; i < buf->dimensions; i++) {
        halide_assert(user_context, buf->dim[i].stride >= 0);
    }


    debug(user_context) << "    allocating " << *buf << "\n";

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    device_handle *dev_handle = (device_handle *)malloc(sizeof(device_handle));
    if (dev_handle == NULL) {
        return CL_OUT_OF_HOST_MEMORY;
    }

    cl_int err;
    debug(user_context) << "    clCreateBuffer -> " << (int)size << " ";
    cl_mem dev_ptr = clCreateBuffer(ctx.context, CL_MEM_READ_WRITE, size, NULL, &err);
    if (err != CL_SUCCESS || dev_ptr == 0) {
        debug(user_context) << get_opencl_error_name(err) << "\n";
        error(user_context) << "CL: clCreateBuffer failed: "
                            << get_opencl_error_name(err);
        free(dev_handle);
        return err;
    } else {
        debug(user_context) << (void *)dev_ptr << " device_handle: " << dev_handle << "\n";
    }

    dev_handle->mem = dev_ptr;
    dev_handle->offset = 0;
    buf->device = (uint64_t)dev_handle;
    buf->device_interface = &opencl_device_interface;
    buf->device_interface->impl->use_module();

    debug(user_context)
        << "    Allocated device buffer " << (void *)buf->device
        << " for buffer " << buf << "\n";

    halide_assert(user_context, validate_device_pointer(user_context, buf, size));

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return CL_SUCCESS;
}

namespace {
WEAK int opencl_do_multidimensional_copy(void *user_context, ClContext &ctx,
                                         const device_copy &c,
                                         int64_t src_idx, int64_t dst_idx,
                                         int d, bool from_host, bool to_host) {
    if (d > MAX_COPY_DIMS) {
        error(user_context) << "Buffer has too many dimensions to copy to/from GPU\n";
        return -1;
    } else if (d == 0) {
        cl_int err = 0;

        debug(user_context) << "    from " << (from_host ? "host" : "device")
                            << " to " << (to_host ? "host" : "device") << ", "
                            << (void *)c.src << " + " << src_idx
                            << " -> " << (void *)c.dst << " + " << dst_idx
                            << ", " << c.chunk_size << " bytes\n";
        if (!from_host && to_host) {
            err = clEnqueueReadBuffer(ctx.cmd_queue, ((device_handle *)c.src)->mem,
                                      CL_FALSE, src_idx + ((device_handle *)c.src)->offset, c.chunk_size, (void *)(c.dst + dst_idx),
                                      0, NULL, NULL);
        } else if (from_host && !to_host) {
            err = clEnqueueWriteBuffer(ctx.cmd_queue, ((device_handle *)c.dst)->mem,
                                       CL_FALSE, dst_idx + ((device_handle *)c.dst)->offset, c.chunk_size, (void *)(c.src + src_idx),
                                       0, NULL, NULL);
        } else if (!from_host && !to_host) {
            err = clEnqueueCopyBuffer(ctx.cmd_queue, ((device_handle *)c.src)->mem, ((device_handle *)c.dst)->mem,
                                      src_idx + ((device_handle *)c.src)->offset, dst_idx  + ((device_handle *)c.dst)->offset,
                                      c.chunk_size, 0, NULL, NULL);
        } else if ((c.dst + dst_idx) != (c.src + src_idx)) {
            // Could reach here if a user called directly into the
            // opencl API for a device->host copy on a source buffer
            // with device_dirty = false.
            memcpy((void *)(c.dst + dst_idx), (void *)(c.src + src_idx), c.chunk_size);
        }

        if (err) {
            error(user_context) << "CL: buffer copy failed: " << get_opencl_error_name(err);
            return (int)err;
        }
    } else {
        ssize_t src_off = 0, dst_off = 0;
        for (int i = 0; i < (int)c.extent[d-1]; i++) {
            int err = opencl_do_multidimensional_copy(user_context, ctx, c,
                                               src_idx + src_off, dst_idx + dst_off,
                                               d - 1, from_host, to_host);
            dst_off += c.dst_stride_bytes[d-1];
            src_off += c.src_stride_bytes[d-1];
            if (err) {
                return err;
            }
        }
    }
    return 0;
}
}

WEAK int halide_opencl_buffer_copy(void *user_context, struct halide_buffer_t *src,
                                   const struct halide_device_interface_t *dst_device_interface,
                                   struct halide_buffer_t *dst) {
    // We only handle copies to opencl or to host
    halide_assert(user_context, dst_device_interface == NULL ||
                  dst_device_interface == &opencl_device_interface);

    if ((src->device_dirty() || src->host == NULL) &&
        src->device_interface != &opencl_device_interface) {
        halide_assert(user_context, dst_device_interface == &opencl_device_interface);
        // This is handled at the higher level.
        return halide_error_code_incompatible_device_interface;
    }

    bool from_host = (src->device_interface != &opencl_device_interface) ||
                     (src->device == 0) ||
                     (src->host_dirty() && src->host != NULL);
    bool to_host = !dst_device_interface;

    halide_assert(user_context, from_host || src->device);
    halide_assert(user_context, to_host || dst->device);

    device_copy c = make_buffer_copy(src, from_host, dst, to_host);

    int err = 0;
    {
        ClContext ctx(user_context);
        if (ctx.error != CL_SUCCESS) {
            return ctx.error;
        }

        debug(user_context)
            << "CL: halide_opencl_buffer_copy (user_context: " << user_context
            << ", src: " << src << ", dst: " << dst << ")\n";

        #ifdef DEBUG_RUNTIME
        uint64_t t_before = halide_current_time_ns(user_context);
        if (!from_host) {
            halide_assert(user_context, validate_device_pointer(user_context, src));
        }
        if (!to_host) {
            halide_assert(user_context, validate_device_pointer(user_context, dst));
        }
        #endif

        err = opencl_do_multidimensional_copy(user_context, ctx, c, c.src_begin, 0, dst->dimensions, from_host, to_host);

        // The reads/writes above are all non-blocking, so empty the command
        // queue before we proceed so that other host code won't write
        // to the buffer while the above writes are still running.
        clFinish(ctx.cmd_queue);

        #ifdef DEBUG_RUNTIME
        uint64_t t_after = halide_current_time_ns(user_context);
        debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
        #endif
    }

    return err;
}

WEAK int halide_opencl_copy_to_device(void *user_context, halide_buffer_t* buf) {
    return halide_opencl_buffer_copy(user_context, buf, &opencl_device_interface, buf);
}

WEAK int halide_opencl_copy_to_host(void *user_context, halide_buffer_t* buf) {
    return halide_opencl_buffer_copy(user_context, buf, NULL, buf);
}

WEAK int halide_opencl_run(void *user_context,
                           void *state_ptr,
                           const char* entry_name,
                           int blocksX, int blocksY, int blocksZ,
                           int threadsX, int threadsY, int threadsZ,
                           int shared_mem_bytes,
                           size_t arg_sizes[],
                           void* args[],
                           int8_t arg_is_buffer[],
                           int num_attributes,
                           float* vertex_buffer,
                           int num_coords_dim0,
                           int num_coords_dim1) {
    debug(user_context)
        << "CL: halide_opencl_run (user_context: " << user_context << ", "
        << "entry: " << entry_name << ", "
        << "blocks: " << blocksX << "x" << blocksY << "x" << blocksZ << ", "
        << "threads: " << threadsX << "x" << threadsY << "x" << threadsZ << ", "
        << "shmem: " << shared_mem_bytes << "\n";


    cl_int err;
    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    // Create kernel object for entry_name from the program for this module.
    halide_assert(user_context, state_ptr);
    cl_program program = ((module_state*)state_ptr)->program;

    halide_assert(user_context, program);
    debug(user_context) << "    clCreateKernel " << entry_name << " -> ";
    cl_kernel f = clCreateKernel(program, entry_name, &err);
    if (err != CL_SUCCESS) {
        debug(user_context) << get_opencl_error_name(err) << "\n";
        error(user_context) << "CL: clCreateKernel " << entry_name << " failed: "
                            << get_opencl_error_name(err) << "\n";
        return err;
    } else {
        #ifdef DEBUG_RUNTIME
        uint64_t t_create_kernel = halide_current_time_ns(user_context);
        debug(user_context) << "    Time: " << (t_create_kernel - t_before) / 1.0e6 << " ms\n";
        #endif
    }

    // Pack dims
    size_t global_dim[3] = {(size_t) blocksX*threadsX,  (size_t) blocksY*threadsY, (size_t) blocksZ*threadsZ};
    size_t local_dim[3] = {(size_t) threadsX, (size_t) threadsY, (size_t) threadsZ};

    // Set args
    int i = 0;

    // Count sub buffers needed for crops.
    int sub_buffers_needed = 0;
    while (arg_sizes[i] != 0) {
        if (arg_is_buffer[i] &&
            ((device_handle *)((halide_buffer_t *)args[i])->device)->offset != 0) {
            sub_buffers_needed++;
        }
        i += 1;
    }
    cl_mem *sub_buffers = NULL;
    int sub_buffers_saved = 0;
    if (sub_buffers_needed > 0) {
        sub_buffers = (cl_mem *)malloc(sizeof(cl_mem) * sub_buffers_needed);
        if (sub_buffers == NULL) {
            return halide_error_code_out_of_memory;
        }
        memset(sub_buffers, 0, sizeof(cl_mem) * sub_buffers_needed);
    }

    i = 0;
    while (arg_sizes[i] != 0) {
        debug(user_context) << "    clSetKernelArg " << i
                            << " " << (int)arg_sizes[i]
                            << " [" << (*((void **)args[i])) << " ...] "
                            << arg_is_buffer[i] << "\n";
        void *this_arg = args[i];
        cl_int err = CL_SUCCESS;

        if (arg_is_buffer[i]) {
            halide_assert(user_context, arg_sizes[i] == sizeof(uint64_t));
            cl_mem mem = ((device_handle *)((halide_buffer_t *)this_arg)->device)->mem;
            uint64_t offset = ((device_handle *)((halide_buffer_t *)this_arg)->device)->offset;

            if (offset != 0) {
                cl_buffer_region region = {(size_t)offset, ((halide_buffer_t *)this_arg)->size_in_bytes()};
                // The sub-buffer encompasses the linear range of addresses that
                // span the crop.
                mem = clCreateSubBuffer(mem, CL_MEM_READ_WRITE, CL_BUFFER_CREATE_TYPE_REGION, &region, &err);
                sub_buffers[sub_buffers_saved++] = mem;
            }
            if (err == CL_SUCCESS) {
                debug(user_context) << "Mapped dev handle is: " << (void *)mem << "\n";
                err = clSetKernelArg(f, i, sizeof(mem), &mem);
            }
        } else {
            err = clSetKernelArg(f, i, arg_sizes[i], this_arg);
        }

        if (err != CL_SUCCESS) {
            error(user_context) << "CL: clSetKernelArg failed: "
                                << get_opencl_error_name(err);
            for (int sub_buf_index = 0; sub_buf_index < sub_buffers_saved; sub_buf_index++) {
                clReleaseMemObject(sub_buffers[sub_buf_index]);
            }
            free(sub_buffers);
            return err;
        }
        i++;
    }
    // Set the shared mem buffer last
    // Always set at least 1 byte of shmem, to keep the launch happy
    debug(user_context)
        << "    clSetKernelArg " << i << " " << shared_mem_bytes << " [NULL]\n";
    err = clSetKernelArg(f, i, (shared_mem_bytes > 0) ? shared_mem_bytes : 1, NULL);
    if (err != CL_SUCCESS) {
        error(user_context) << "CL: clSetKernelArg failed "
                            << get_opencl_error_name(err);
        return err;
    }

    // Launch kernel
    debug(user_context)
        << "    clEnqueueNDRangeKernel "
        << blocksX << "x" << blocksY << "x" << blocksZ << ", "
        << threadsX << "x" << threadsY << "x" << threadsZ << " -> ";
    err = clEnqueueNDRangeKernel(ctx.cmd_queue, f,
                                 // NDRange
                                 3, NULL, global_dim, local_dim,
                                 // Events
                                 0, NULL, NULL);
    debug(user_context) << get_opencl_error_name(err) << "\n";

    // Now that the kernel is enqueued, OpenCL is holding its own
    // references to sub buffers and the local ones can be released.
    for (int sub_buf_index = 0; sub_buf_index < sub_buffers_saved; sub_buf_index++) {
        clReleaseMemObject(sub_buffers[sub_buf_index]);
    }
    free(sub_buffers);

    if (err != CL_SUCCESS) {
        error(user_context) << "CL: clEnqueueNDRangeKernel failed: "
                            << get_opencl_error_name(err) << "\n";
        return err;
    }

    debug(user_context) << "    Releasing kernel " << (void *)f << "\n";
    clReleaseKernel(f);
    debug(user_context) << "    clReleaseKernel finished" << (void *)f << "\n";

    #ifdef DEBUG_RUNTIME
    err = clFinish(ctx.cmd_queue);
    if (err != CL_SUCCESS) {
        error(user_context) << "CL: clFinish failed (" << err << ")\n";
        return err;
    }
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif
    return 0;
}

WEAK int halide_opencl_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf) {
    return halide_default_device_and_host_malloc(user_context, buf, &opencl_device_interface);
}

WEAK int halide_opencl_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    return halide_default_device_and_host_free(user_context, buf, &opencl_device_interface);
}

WEAK int halide_opencl_wrap_cl_mem(void *user_context, struct halide_buffer_t *buf, uint64_t mem) {
    halide_assert(user_context, buf->device == 0);
    if (buf->device != 0) {
        return -2;
    }
    device_handle *dev_handle = (device_handle *)malloc(sizeof(device_handle));
    if (dev_handle == NULL) {
        return halide_error_code_out_of_memory;
    }
    dev_handle->mem = (cl_mem)mem;
    dev_handle->offset = 0;
    buf->device = (uint64_t)dev_handle;
    buf->device_interface = &opencl_device_interface;
    buf->device_interface->impl->use_module();
#ifdef DEBUG_RUNTIME
    if (!validate_device_pointer(user_context, buf)) {
        free((device_handle *)buf->device);
        buf->device = 0;
        buf->device_interface->impl->release_module();
        buf->device_interface = NULL;
        return -3;
    }
#endif
    return 0;
}

WEAK int halide_opencl_detach_cl_mem(void *user_context, halide_buffer_t *buf) {
    if (buf->device == NULL) {
        return 0;
    }
    halide_assert(user_context, buf->device_interface == &opencl_device_interface);
    free((device_handle *)buf->device);
    buf->device = 0;
    buf->device_interface->impl->release_module();
    buf->device_interface = NULL;
    return 0;
}

WEAK uintptr_t halide_opencl_get_cl_mem(void *user_context, halide_buffer_t *buf) {
    if (buf->device == NULL) {
        return 0;
    }
    halide_assert(user_context, buf->device_interface == &opencl_device_interface);
    return (uintptr_t)((device_handle *)buf->device)->mem;
}

WEAK uint64_t halide_opencl_get_crop_offset(void *user_context, halide_buffer_t *buf) {
    if (buf->device == NULL) {
        return 0;
    }
    halide_assert(user_context, buf->device_interface == &opencl_device_interface);
    return ((device_handle *)buf->device)->offset;
}

namespace {

WEAK int opencl_device_crop_from_offset(void *user_context,
                                        const struct halide_buffer_t *src,
                                        int64_t offset,
                                        struct halide_buffer_t *dst) {
    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    dst->device_interface = src->device_interface;

    device_handle *new_dev_handle = (device_handle *)malloc(sizeof(device_handle));
    if (new_dev_handle == NULL) {
        error(user_context) << "CL: malloc failed making device handle for crop.\n";
        return halide_error_code_out_of_memory;
    }

    clRetainMemObject(((device_handle *)src->device)->mem);
    new_dev_handle->mem = ((device_handle *)src->device)->mem;
    new_dev_handle->offset = ((device_handle *)src->device)->offset + offset;
    dst->device = (uint64_t)new_dev_handle;

    return 0;
}

}  // namespace

WEAK int halide_opencl_device_crop(void *user_context,
                                    const struct halide_buffer_t *src,
                                    struct halide_buffer_t *dst) {
    const int64_t offset = calc_device_crop_byte_offset(src, dst);
    return opencl_device_crop_from_offset(user_context, src, offset, dst);
}

WEAK int halide_opencl_device_slice(void *user_context,
                                    const struct halide_buffer_t *src,
                                    int slice_dim,
                                    int slice_pos,
                                    struct halide_buffer_t *dst) {
    const int64_t offset = calc_device_slice_byte_offset(src, slice_dim, slice_pos);
    return opencl_device_crop_from_offset(user_context, src, offset, dst);
}

WEAK int halide_opencl_device_release_crop(void *user_context,
                                            struct halide_buffer_t *buf) {
    // Basically the same code as in halide_opencl_device_free, but with
    // enough differences to require separate code.

    cl_mem dev_ptr = ((device_handle *)buf->device)->mem;

    debug(user_context)
        << "CL: halide_opencl_device_release_crop(user_context: " << user_context
        << ", buf: " << buf << ") cl_mem: " << dev_ptr << " offset: " << ((device_handle *)buf->device)->offset << "\n";

    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, validate_device_pointer(user_context, buf));
    debug(user_context) << "    clReleaseMemObject " << (void *)dev_ptr << "\n";
    // Sub-buffers are released with clReleaseMemObject
    cl_int result = clReleaseMemObject((cl_mem)dev_ptr);
    free((device_handle *)buf->device);
    if (result != CL_SUCCESS) {
        // We may be called as a destructor, so don't raise an error
        // here.
        return result;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK const struct halide_device_interface_t *halide_opencl_device_interface() {
    return &opencl_device_interface;
}

namespace {
__attribute__((destructor))
WEAK void halide_opencl_cleanup() {
    halide_opencl_device_release(NULL);
}
}

} // extern "C" linkage

namespace Halide { namespace Runtime { namespace Internal { namespace OpenCL {
WEAK const char *get_opencl_error_name(cl_int err) {
    switch (err) {
    case CL_SUCCESS: return "CL_SUCCESS";
    case CL_DEVICE_NOT_FOUND: return "CL_DEVICE_NOT_FOUND";
    case CL_DEVICE_NOT_AVAILABLE: return "CL_DEVICE_NOT_AVAILABLE";
    case CL_COMPILER_NOT_AVAILABLE: return "CL_COMPILER_NOT_AVAILABLE";
    case CL_MEM_OBJECT_ALLOCATION_FAILURE: return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
    case CL_OUT_OF_RESOURCES: return "CL_OUT_OF_RESOURCES";
    case CL_OUT_OF_HOST_MEMORY: return "CL_OUT_OF_HOST_MEMORY";
    case CL_PROFILING_INFO_NOT_AVAILABLE: return "CL_PROFILING_INFO_NOT_AVAILABLE";
    case CL_MEM_COPY_OVERLAP: return "CL_MEM_COPY_OVERLAP";
    case CL_IMAGE_FORMAT_MISMATCH: return "CL_IMAGE_FORMAT_MISMATCH";
    case CL_IMAGE_FORMAT_NOT_SUPPORTED: return "CL_IMAGE_FORMAT_NOT_SUPPORTED";
    case CL_BUILD_PROGRAM_FAILURE: return "CL_BUILD_PROGRAM_FAILURE";
    case CL_MAP_FAILURE: return "CL_MAP_FAILURE";
    case CL_MISALIGNED_SUB_BUFFER_OFFSET: return "CL_MISALIGNED_SUB_BUFFER_OFFSET";
    case CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST: return "CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST";
    case CL_COMPILE_PROGRAM_FAILURE: return "CL_COMPILE_PROGRAM_FAILURE";
    case CL_LINKER_NOT_AVAILABLE: return "CL_LINKER_NOT_AVAILABLE";
    case CL_LINK_PROGRAM_FAILURE: return "CL_LINK_PROGRAM_FAILURE";
    case CL_DEVICE_PARTITION_FAILED: return "CL_DEVICE_PARTITION_FAILED";
    case CL_KERNEL_ARG_INFO_NOT_AVAILABLE: return "CL_KERNEL_ARG_INFO_NOT_AVAILABLE";
    case CL_INVALID_VALUE: return "CL_INVALID_VALUE";
    case CL_INVALID_DEVICE_TYPE: return "CL_INVALID_DEVICE_TYPE";
    case CL_INVALID_PLATFORM: return "CL_INVALID_PLATFORM";
    case CL_INVALID_DEVICE: return "CL_INVALID_DEVICE";
    case CL_INVALID_CONTEXT: return "CL_INVALID_CONTEXT";
    case CL_INVALID_QUEUE_PROPERTIES: return "CL_INVALID_QUEUE_PROPERTIES";
    case CL_INVALID_COMMAND_QUEUE: return "CL_INVALID_COMMAND_QUEUE";
    case CL_INVALID_HOST_PTR: return "CL_INVALID_HOST_PTR";
    case CL_INVALID_MEM_OBJECT: return "CL_INVALID_MEM_OBJECT";
    case CL_INVALID_IMAGE_FORMAT_DESCRIPTOR: return "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR";
    case CL_INVALID_IMAGE_SIZE: return "CL_INVALID_IMAGE_SIZE";
    case CL_INVALID_SAMPLER: return "CL_INVALID_SAMPLER";
    case CL_INVALID_BINARY: return "CL_INVALID_BINARY";
    case CL_INVALID_BUILD_OPTIONS: return "CL_INVALID_BUILD_OPTIONS";
    case CL_INVALID_PROGRAM: return "CL_INVALID_PROGRAM";
    case CL_INVALID_PROGRAM_EXECUTABLE: return "CL_INVALID_PROGRAM_EXECUTABLE";
    case CL_INVALID_KERNEL_NAME: return "CL_INVALID_KERNEL_NAME";
    case CL_INVALID_KERNEL_DEFINITION: return "CL_INVALID_KERNEL_DEFINITION";
    case CL_INVALID_KERNEL: return "CL_INVALID_KERNEL";
    case CL_INVALID_ARG_INDEX: return "CL_INVALID_ARG_INDEX";
    case CL_INVALID_ARG_VALUE: return "CL_INVALID_ARG_VALUE";
    case CL_INVALID_ARG_SIZE: return "CL_INVALID_ARG_SIZE";
    case CL_INVALID_KERNEL_ARGS: return "CL_INVALID_KERNEL_ARGS";
    case CL_INVALID_WORK_DIMENSION: return "CL_INVALID_WORK_DIMENSION";
    case CL_INVALID_WORK_GROUP_SIZE: return "CL_INVALID_WORK_GROUP_SIZE";
    case CL_INVALID_WORK_ITEM_SIZE: return "CL_INVALID_WORK_ITEM_SIZE";
    case CL_INVALID_GLOBAL_OFFSET: return "CL_INVALID_GLOBAL_OFFSET";
    case CL_INVALID_EVENT_WAIT_LIST: return "CL_INVALID_EVENT_WAIT_LIST";
    case CL_INVALID_EVENT: return "CL_INVALID_EVENT";
    case CL_INVALID_OPERATION: return "CL_INVALID_OPERATION";
    case CL_INVALID_GL_OBJECT: return "CL_INVALID_GL_OBJECT";
    case CL_INVALID_BUFFER_SIZE: return "CL_INVALID_BUFFER_SIZE";
    case CL_INVALID_MIP_LEVEL: return "CL_INVALID_MIP_LEVEL";
    case CL_INVALID_GLOBAL_WORK_SIZE: return "CL_INVALID_GLOBAL_WORK_SIZE";
    case CL_INVALID_PROPERTY: return "CL_INVALID_PROPERTY";
    case CL_INVALID_IMAGE_DESCRIPTOR: return "CL_INVALID_IMAGE_DESCRIPTOR";
    case CL_INVALID_COMPILER_OPTIONS: return "CL_INVALID_COMPILER_OPTIONS";
    case CL_INVALID_LINKER_OPTIONS: return "CL_INVALID_LINKER_OPTIONS";
    case CL_INVALID_DEVICE_PARTITION_COUNT: return "CL_INVALID_DEVICE_PARTITION_COUNT";
    default: return "<Unknown error>";
    }
}

WEAK halide_device_interface_impl_t opencl_device_interface_impl = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_opencl_device_malloc,
    halide_opencl_device_free,
    halide_opencl_device_sync,
    halide_opencl_device_release,
    halide_opencl_copy_to_host,
    halide_opencl_copy_to_device,
    halide_opencl_device_and_host_malloc,
    halide_opencl_device_and_host_free,
    halide_opencl_buffer_copy,
    halide_opencl_device_crop,
    halide_opencl_device_slice,
    halide_opencl_device_release_crop,
    halide_opencl_wrap_cl_mem,
    halide_opencl_detach_cl_mem,
};

WEAK halide_device_interface_t opencl_device_interface = {
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
    &opencl_device_interface_impl
};

}}}} // namespace Halide::Runtime::Internal::OpenCL
