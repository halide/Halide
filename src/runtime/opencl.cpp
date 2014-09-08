#include "runtime_internal.h"
#include "scoped_spin_lock.h"
#include "../buffer_t.h"
#include "HalideRuntime.h"

#include "mini_cl.h"

#include "cuda_opencl_shared.h"

namespace Halide { namespace Runtime { namespace Internal {

WEAK const char *get_opencl_error_name(cl_int err);
WEAK int create_opencl_context(void *user_context, cl_context *ctx, cl_command_queue *q);

// An OpenCL context/queue/synchronization lock defined in
// this module with weak linkage
cl_context WEAK weak_cl_ctx = 0;
cl_command_queue WEAK weak_cl_q = 0;
volatile int WEAK weak_cl_lock = 0;

// In the non-JIT case, the context is stored in the weak globals above.
// JIT modules will call halide_set_cl_context, changing the pointers below.
cl_context WEAK *cl_ctx_ptr = NULL;
cl_command_queue WEAK *cl_q_ptr = NULL;
volatile int WEAK *cl_lock_ptr = NULL;

}}} // namespace Halide::Runtime::Internal

// Allow OpenCL 1.1 features to be used.
#define ENABLE_OPENCL_11

extern "C" {

extern int64_t halide_current_time_ns(void *user_context);
extern void free(void *);
extern void *malloc(size_t);
extern int snprintf(char *, size_t, const char *, ...);
extern const char * strstr(const char *, const char *);
extern int atoi(const char *);

WEAK void halide_set_cl_context(cl_context* ctx_ptr, cl_command_queue* q_ptr, volatile int* lock_ptr) {
    cl_ctx_ptr = ctx_ptr;
    cl_q_ptr = q_ptr;
    cl_lock_ptr = lock_ptr;
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
WEAK int halide_acquire_cl_context(void *user_context, cl_context *ctx, cl_command_queue *q) {
    // TODO: Should we use a more "assertive" assert? These asserts do
    // not block execution on failure.
    halide_assert(user_context, ctx != NULL);
    halide_assert(user_context, q != NULL);

    // If the context pointers aren't hooked up, use our weak globals.
    if (cl_ctx_ptr == NULL) {
        cl_ctx_ptr = &weak_cl_ctx;
        cl_q_ptr = &weak_cl_q;
        cl_lock_ptr = &weak_cl_lock;
    }

    halide_assert(user_context, cl_lock_ptr != NULL);
    while (__sync_lock_test_and_set(cl_lock_ptr, 1)) { }

    // If the context has not been initialized, initialize it now.
    halide_assert(user_context, cl_ctx_ptr != NULL);
    halide_assert(user_context, cl_q_ptr != NULL);
    if (!(*cl_ctx_ptr)) {
        cl_int error = create_opencl_context(user_context, cl_ctx_ptr, cl_q_ptr);
        if (error != CL_SUCCESS) {
            __sync_lock_release(cl_lock_ptr);
            return error;
        }
    }

    *ctx = *cl_ctx_ptr;
    *q = *cl_q_ptr;
    return 0;
}

WEAK int halide_release_cl_context(void *user_context) {
    __sync_lock_release(cl_lock_ptr);
    return 0;
}

} // extern "C"

namespace Halide { namespace Runtime { namespace Internal {

// Helper object to acquire and release the OpenCL context.
class ClContext {
    void *user_context;

public:
    cl_context context;
    cl_command_queue cmd_queue;
    cl_int error;

    // Constructor sets 'error' if any occurs.
    ClContext(void *user_context) : user_context(user_context),
                                    context(NULL),
                                    cmd_queue(NULL),
                                    error(CL_SUCCESS) {
        error = halide_acquire_cl_context(user_context, &context, &cmd_queue);
        halide_assert(user_context, context != NULL && cmd_queue != NULL);
    }

    ~ClContext() {
        halide_release_cl_context(user_context);
    }
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

WEAK bool validate_dev_pointer(void *user_context, buffer_t* buf, size_t size=0) {
    if (buf->dev == 0) {
        return true;
    }

    size_t real_size;
    cl_int result = clGetMemObjectInfo((cl_mem)buf->dev, CL_MEM_SIZE, sizeof(size_t), &real_size, NULL);
    if (result != CL_SUCCESS) {
        halide_printf(user_context, "CL: Bad device pointer %p: clGetMemObjectInfo returned %s\n",
                      (void *)buf->dev, get_opencl_error_name(result));
        return false;
    }

    DEBUG_PRINTF( user_context, "CL: validate %p: asked for %lld, actual allocated %lld\n",
                  (void*)buf->dev, (long long)size, (long long)real_size );

    if (size) halide_assert(user_context, real_size >= size && "Validating pointer with insufficient size");
    return true;
}

// Initializes the context used by the default implementation
// of halide_acquire_context.
WEAK int create_opencl_context(void *user_context, cl_context *ctx, cl_command_queue *q) {
    DEBUG_PRINTF( user_context, "    create_opencl_context (user_context: %p)\n", user_context );

    halide_assert(user_context, ctx != NULL && *ctx == NULL);
    halide_assert(user_context, q != NULL && *q == NULL);

    cl_int err = 0;

    const cl_uint maxPlatforms = 4;
    cl_platform_id platforms[maxPlatforms];
    cl_uint platformCount = 0;

    err = clGetPlatformIDs( maxPlatforms, platforms, &platformCount );
    if (err != CL_SUCCESS) {
        halide_error_varargs(user_context, "CL: clGetPlatformIDs failed (%s)\n",
                             get_opencl_error_name(err));
        return err;
    }

    cl_platform_id platform = NULL;

    // Find the requested platform, or the first if none specified.
    const char * name = halide_get_ocl_platform_name(user_context);
    if (name != NULL) {
        for (cl_uint i = 0; i < platformCount; ++i) {
            const cl_uint maxPlatformName = 256;
            char platformName[maxPlatformName];
            err = clGetPlatformInfo( platforms[i], CL_PLATFORM_NAME, maxPlatformName, platformName, NULL );
            if (err != CL_SUCCESS) continue;

            // A platform matches the request if it is a substring of the platform name.
            if (strstr(platformName, name)) {
                platform = platforms[i];
                break;
            }
        }
    } else if (platformCount > 0) {
        platform = platforms[0];
    }
    if (platform == NULL){
        halide_error(user_context, "CL: Failed to find platform\n");
        return CL_INVALID_PLATFORM;
    }

    #ifdef DEBUG
    const cl_uint maxPlatformName = 256;
    char platformName[maxPlatformName];
    err = clGetPlatformInfo( platform, CL_PLATFORM_NAME, maxPlatformName, platformName, NULL );
    if (err != CL_SUCCESS) {
        halide_printf(user_context, "    clGetPlatformInfo(CL_PLATFORM_NAME) failed (%s)\n",
                      get_opencl_error_name(err));
        // This is just debug info, report the error but don't fail context creation due to it.
        //return err;
    } else {
        halide_printf(user_context, "    Got platform '%s', about to create context (t=%lld)\n",
                      platformName, (long long)halide_current_time_ns(user_context));
    }
    #endif

    // Get the types of devices requested.
    cl_device_type device_type = 0;
    const char * dev_type = halide_get_ocl_device_type(user_context);
    if (dev_type != NULL) {
        if (strstr("cpu", dev_type)) {
            device_type |= CL_DEVICE_TYPE_CPU;
        }
        if (strstr("gpu", dev_type)) {
            device_type |= CL_DEVICE_TYPE_GPU;
        }
        if (strstr("acc", dev_type)) {
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
    err = clGetDeviceIDs( platform, device_type, maxDevices, devices, &deviceCount );
    if (err != CL_SUCCESS) {
        halide_error_varargs(user_context, "CL: clGetDeviceIDs failed (%s)\n",
                             get_opencl_error_name(err));
        return err;
    }

    // If the user indicated a specific device index to use, use
    // that. Note that this is an index within the set of devices
    // specified by the device type. -1 means the last device.
    int device = halide_get_gpu_device(user_context);
    if (device == -1) {
        device = deviceCount - 1;
    }

    if (device < 0 || device >= deviceCount) {
        halide_error_varargs(user_context, "CL: Failed to get device %d\n", device);
        return CL_DEVICE_NOT_FOUND;
    }

    cl_device_id dev = devices[device];

    #ifdef DEBUG
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
            halide_error_varargs(user_context, "CL: clGetDeviceInfo failed (%s)\n", get_opencl_error_name(err));
            return err;
        }
    }

    halide_printf(user_context,
                  "      device name: %s\n"
                  "      device vendor: %s\n"
                  "      device profile: %s\n"
                  "      global mem size: %d MB\n"
                  "      max mem alloc size: %d MB\n"
                  "      local mem size: %lu\n"
                  "      max compute units: %d\n"
                  "      max workgroup size: %lu\n"
                  "      max work item dimensions: %d\n"
                  "      max work item sizes: %lux%lux%lux%lu\n",
                  device_name, device_vendor, device_profile,
                  (int)(global_mem_size/(1024*1024)),
                  (int)(max_mem_alloc_size/(1024*1024)),
                  local_mem_size,
                  max_compute_units, (cl_ulong)max_work_group_size, max_work_item_dimensions,
                  (cl_ulong)max_work_item_sizes[0], (cl_ulong)max_work_item_sizes[1],
                  (cl_ulong)max_work_item_sizes[2], (cl_ulong)max_work_item_sizes[3]);

    #endif


    // Create context and command queue.
    cl_context_properties properties[] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0 };
    DEBUG_PRINTF( user_context, "    clCreateContext -> " );
    *ctx = clCreateContext(properties, 1, &dev, NULL, NULL, &err);
    if (err != CL_SUCCESS) {
        DEBUG_PRINTF( user_context, "%s", get_opencl_error_name(err) );
        halide_error_varargs(user_context, "CL: clCreateContext failed (%s)\n",
                             get_opencl_error_name(err));
        return err;
    } else {
        DEBUG_PRINTF( user_context, "%p\n", *ctx );
    }

    DEBUG_PRINTF(user_context, "    clCreateCommandQueue ");
    *q = clCreateCommandQueue(*ctx, dev, 0, &err);
    if (err != CL_SUCCESS) {
        DEBUG_PRINTF( user_context, "%s", get_opencl_error_name(err) );
        halide_error_varargs(user_context, "CL: clCreateCommandQueue failed (%d)\n",
                             get_opencl_error_name(err));
        return err;
    } else {
        DEBUG_PRINTF( user_context, "%p\n", *q );
    }

    return err;
}

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK int halide_dev_free(void *user_context, buffer_t* buf) {

    // halide_dev_free, at present, can be exposed to clients and they
    // should be allowed to call halide_dev_free on any buffer_t
    // including ones that have never been used with a GPU.
    if (buf->dev == 0) {
      return 0;
    }

    DEBUG_PRINTF( user_context, "CL: halide_dev_free (user_context: %p, buf: %p)\n", user_context, buf );

    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, validate_dev_pointer(user_context, buf));
    DEBUG_PRINTF(user_context, "    clReleaseMemObject %p\n", (cl_mem)buf->dev );
    cl_int result = clReleaseMemObject((cl_mem)buf->dev);
    // If clReleaseMemObject fails, it is unlikely to succeed in a later call, so
    // we just end our reference to it regardless.
    buf->dev = 0;
    if (result != CL_SUCCESS) {
        halide_error_varargs(user_context, "CL: clReleaseMemObject failed (%s)",
                             get_opencl_error_name(result));
        return result;
    }

    #ifdef DEBUG
    uint64_t t_after = halide_current_time_ns(user_context);
    halide_printf(user_context, "    Time: %f ms\n", (t_after - t_before) / 1.0e6);
    #endif

    return 0;
}


WEAK int halide_init_kernels(void *user_context, void **state_ptr, const char* src, int size) {
    DEBUG_PRINTF( user_context, "CL: halide_init_kernels (user_context: %p, state_ptr: %p, program: %p, %i)\n",
                  user_context, state_ptr, src, size );

    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG
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
            halide_error_varargs(user_context, "CL: clGetContextInfo(CL_CONTEXT_DEVICES) failed (%s)\n",
                                 get_opencl_error_name(err));
            return err;
        }

        cl_device_id devices[] = { dev };

        // Get the max constant buffer size supported by this OpenCL implementation.
        cl_ulong max_constant_buffer_size = 0;
        err = clGetDeviceInfo(dev, CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE, sizeof(max_constant_buffer_size), &max_constant_buffer_size, NULL);
        if (err != CL_SUCCESS) {
            halide_error_varargs(user_context, "CL: clGetDeviceInfo (CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE) failed (%s)\n",
                                 get_opencl_error_name(err));
            return err;
        }
        // Get the max number of constant arguments supported by this OpenCL implementation.
        cl_uint max_constant_args = 0;
        err = clGetDeviceInfo(dev, CL_DEVICE_MAX_CONSTANT_ARGS, sizeof(max_constant_args), &max_constant_args, NULL);
        if (err != CL_SUCCESS) {
            halide_error_varargs(user_context, "CL: clGetDeviceInfo (CL_DEVICE_MAX_CONSTANT_ARGS) failed (%s)\n",
                                 get_opencl_error_name(err));
            return err;
        }

        // Build the compile argument options.
        char options[256];
        snprintf(options, sizeof(options),
                 "-D MAX_CONSTANT_BUFFER_SIZE=%lld -D MAX_CONSTANT_ARGS=%i",
                 max_constant_buffer_size,
                 max_constant_args);

        const char * sources[] = { src };
        DEBUG_PRINTF( user_context, "    clCreateProgramWithSource -> " );
        cl_program program = clCreateProgramWithSource(ctx.context, 1, &sources[0], NULL, &err );
        if (err != CL_SUCCESS) {
            DEBUG_PRINTF( user_context, "%s\n", get_opencl_error_name(err) );
            halide_error_varargs(user_context, "CL: clCreateProgramWithSource failed (%s)\n",
                                 get_opencl_error_name(err));
            return err;
        } else {
            DEBUG_PRINTF( user_context, "%p\n", program );
        }
        (*state)->program = program;

        DEBUG_PRINTF( user_context, "    clBuildProgram %p %s\n", program, options );
        err = clBuildProgram(program, 1, devices, options, NULL, NULL );
        if (err != CL_SUCCESS) {
            halide_error_varargs(user_context, "CL: clBuildProgram failed (%s)\n",
                                 get_opencl_error_name(err));

            // Allocate an appropriately sized buffer for the build log.
            size_t len = 0;
            char *buffer = NULL;
            if (clGetProgramBuildInfo(program,
                                      dev,
                                      CL_PROGRAM_BUILD_LOG,
                                      0, NULL,
                                      &len) == CL_SUCCESS) {
                buffer = (char*)malloc((++len)*sizeof(char));
            }

            // Get build log
            if (buffer && clGetProgramBuildInfo(program,
                                                dev,
                                                CL_PROGRAM_BUILD_LOG,
                                                len, buffer,
                                                NULL) == CL_SUCCESS) {
                halide_printf(user_context, "    Build Log:\n %s\n-----\n", buffer);
            } else {
                halide_printf(user_context, "    clGetProgramBuildInfo failed\n");
            }

            if (buffer) {
                free(buffer);
            }

            halide_assert(user_context, err == CL_SUCCESS);
            return err;
        }
    }

    #ifdef DEBUG
    uint64_t t_after = halide_current_time_ns(user_context);
    halide_printf(user_context, "    Time: %f ms\n", (t_after - t_before) / 1.0e6);
    #endif

    return 0;
}

// Used to generate correct timings when tracing
WEAK int halide_dev_sync(void *user_context) {
    DEBUG_PRINTF( user_context, "CL: halide_dev_sync (user_context: %p)\n", user_context );

    ClContext ctx(user_context);
    halide_assert(user_context, ctx.error == CL_SUCCESS);

    #ifdef DEBUG
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    cl_int err = clFinish(ctx.cmd_queue);
    if (err != CL_SUCCESS) {
        halide_error_varargs(user_context, "CL: clFinish failed (%s)\n",
                             get_opencl_error_name(err));
        return err;
    }

    #ifdef DEBUG
    uint64_t t_after = halide_current_time_ns(user_context);
    halide_printf(user_context, "    Time: %f ms\n", (t_after - t_before) / 1.0e6);
    #endif

    return CL_SUCCESS;
}

WEAK void halide_release(void *user_context) {
    DEBUG_PRINTF( user_context, "CL: halide_release (user_context: %p)\n", user_context );

    // The ClContext object does not allow the context storage to be modified,
    // so we use halide_acquire_context directly.
    int err;
    cl_context ctx;
    cl_command_queue q;
    err = halide_acquire_cl_context(user_context, &ctx, &q);
    if (err != 0 || !ctx) {
        return;
    }

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
            DEBUG_PRINTF(user_context, "    clReleaseProgram %p\n", state->program);
            err = clReleaseProgram(state->program);
            halide_assert(user_context, err == CL_SUCCESS);
            state->program = NULL;
        }
        state = state->next;
    }

    // Release the context itself, if we created it.
    if (ctx == weak_cl_ctx) {
        DEBUG_PRINTF( user_context, "    clReleaseCommandQueue %p\n", weak_cl_q );
        err = clReleaseCommandQueue(weak_cl_q);
        halide_assert(user_context, err == CL_SUCCESS);
        weak_cl_q = NULL;

        DEBUG_PRINTF( user_context, "    clReleaseContext %p\n", weak_cl_ctx );
        err = clReleaseContext(weak_cl_ctx);
        halide_assert(user_context, err == CL_SUCCESS);
        weak_cl_ctx = NULL;
    }

    halide_release_cl_context(user_context);
}

WEAK int halide_dev_malloc(void *user_context, buffer_t* buf) {
    DEBUG_PRINTF( user_context, "CL: halide_dev_malloc (user_context: %p, buf: %p)\n", user_context, buf );

    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    size_t size = buf_size(user_context, buf);
    if (buf->dev) {
        halide_assert(user_context, validate_dev_pointer(user_context, buf, size));
        return 0;
    }

    halide_assert(user_context, buf->stride[0] >= 0 && buf->stride[1] >= 0 &&
                                buf->stride[2] >= 0 && buf->stride[3] >= 0);

    DEBUG_PRINTF(user_context, "    Allocating buffer of %lld bytes, "
                 "extents: %lldx%lldx%lldx%lld strides: %lldx%lldx%lldx%lld (%d bytes per element)\n",
                 (long long)size,
                 (long long)buf->extent[0], (long long)buf->extent[1],
                 (long long)buf->extent[2], (long long)buf->extent[3],
                 (long long)buf->stride[0], (long long)buf->stride[1],
                 (long long)buf->stride[2], (long long)buf->stride[3],
                 buf->elem_size);

    #ifdef DEBUG
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    cl_int err;
    DEBUG_PRINTF( user_context, "    clCreateBuffer -> ", size );
    buf->dev = (uint64_t)clCreateBuffer(ctx.context, CL_MEM_READ_WRITE, size, NULL, &err);
    if (err != CL_SUCCESS || buf->dev == 0) {
        DEBUG_PRINTF( user_context, "%s\n",
                      get_opencl_error_name(err));
        halide_error_varargs(user_context, "CL: clCreateBuffer failed (%s)\n",
                             get_opencl_error_name(err));
        return err;
    } else {
        DEBUG_PRINTF( user_context, "%p\n", (cl_mem)buf->dev );
    }

    DEBUG_PRINTF(user_context, "    Allocated device buffer %p for buffer %p\n",
                 (void *)buf->dev, buf);

    #ifdef DEBUG
    uint64_t t_after = halide_current_time_ns(user_context);
    halide_printf(user_context, "    Time: %f ms\n", (t_after - t_before) / 1.0e6);
    #endif

    return CL_SUCCESS;
}

WEAK int halide_copy_to_dev(void *user_context, buffer_t* buf) {
    int err = halide_dev_malloc(user_context, buf);
    if (err) {
        return err;
    }

    DEBUG_PRINTF(user_context, "CL: halide_copy_to_dev (user_context: %p, buf: %p)\n", user_context, buf );

    // Acquire the context so we can use the command queue. This also avoids multiple
    // redundant calls to clEnqueueWriteBuffer when multiple threads are trying to copy
    // the same buffer.
    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    if (buf->host_dirty) {
        #ifdef DEBUG
        uint64_t t_before = halide_current_time_ns(user_context);
        #endif

        halide_assert(user_context, buf->host && buf->dev);
        halide_assert(user_context, validate_dev_pointer(user_context, buf));

        dev_copy c = make_host_to_dev_copy(buf);

        for (int w = 0; w < c.extent[3]; w++) {
            for (int z = 0; z < c.extent[2]; z++) {
#ifdef ENABLE_OPENCL_11
                // OpenCL 1.1 supports stride-aware memory transfers up to 3D, so we
                // can deal with the 2 innermost strides with OpenCL.
                uint64_t off = z * c.stride_bytes[2] + w * c.stride_bytes[3];

                size_t offset[3] = { off, 0, 0 };
                size_t region[3] = { c.chunk_size, c.extent[0], c.extent[1] };

                DEBUG_PRINTF( user_context, "    clEnqueueWriteBufferRect ((%d, %d), (%p -> %p) + %d, %dx%dx%d bytes, %dx%d)\n",
                              z, w,
                              (void *)c.src, c.dst, (int)off,
                              (int)region[0], (int)region[1], (int)region[2],
                              (int)c.stride_bytes[0], (int)c.stride_bytes[1]);

                cl_int err = clEnqueueWriteBufferRect(ctx.cmd_queue, (cl_mem)c.dst, CL_FALSE,
                                                      offset, offset, region,
                                                      c.stride_bytes[0], c.stride_bytes[1],
                                                      c.stride_bytes[0], c.stride_bytes[1],
                                                      (void *)c.src,
                                                      0, NULL, NULL);

                if (err != CL_SUCCESS) {
                    halide_error_varargs(user_context, "CL: clEnqueueWriteBufferRect failed (%s)\n",
                                         get_opencl_error_name(err));
                    return err;
                }
#else
                for (int y = 0; y < c.extent[1]; y++) {
                    for (int x = 0; x < c.extent[0]; x++) {
                        uint64_t off = (x * c.stride_bytes[0] +
                                        y * c.stride_bytes[1] +
                                        z * c.stride_bytes[2] +
                                        w * c.stride_bytes[3]);
                        void *src = (void *)(c.src + off);
                        void *dst = (void *)(c.dst + off);
                        uint64_t size = c.chunk_size;

                        DEBUG_PRINTF( user_context, "    clEnqueueWriteBuffer ((%d, %d, %d, %d), %lld bytes, %p -> %p)\n",
                                      x, y, z, w,
                                      (long long)size, src, (void *)dst );
                        cl_int err = clEnqueueWriteBuffer(ctx.cmd_queue, (cl_mem)c.dst,
                                                          CL_FALSE, off, size, src, 0, NULL, NULL);
                        if (err != CL_SUCCESS) {
                            halide_error_varargs(user_context, "CL: clEnqueueWriteBuffer failed (%s)\n",
                                                 get_opencl_error_name(err));
                            return err;
                        }
                    }
                }
#endif
            }
        }
        // The writes above are all non-blocking, so empty the command
        // queue before we proceed so that other host code won't write
        // to the buffer while the above writes are still running.
        clFinish(ctx.cmd_queue);

        #ifdef DEBUG
        uint64_t t_after = halide_current_time_ns(user_context);
        halide_printf(user_context, "    Time: %f ms\n", (t_after - t_before) / 1.0e6);
        #endif
    }
    buf->host_dirty = false;
    return 0;
}

WEAK int halide_copy_to_host(void *user_context, buffer_t* buf) {
    if (!buf->dev_dirty) {
        return 0;
    }

    DEBUG_PRINTF(user_context, "CL: halide_copy_to_host (user_context: %p, buf: %p)\n", user_context, buf );

    // Acquire the context so we can use the command queue. This also avoids multiple
    // redundant calls to clEnqueueReadBuffer when multiple threads are trying to copy
    // the same buffer.
    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    // Need to check dev_dirty again, in case another thread did the
    // copy_to_host before the serialization point above.
    if (buf->dev_dirty) {
        #ifdef DEBUG
        uint64_t t_before = halide_current_time_ns(user_context);
        #endif

        halide_assert(user_context, buf->host && buf->dev);
        halide_assert(user_context, validate_dev_pointer(user_context, buf));

        dev_copy c = make_dev_to_host_copy(buf);

        for (int w = 0; w < c.extent[3]; w++) {
            for (int z = 0; z < c.extent[2]; z++) {
#ifdef ENABLE_OPENCL_11
                // OpenCL 1.1 supports stride-aware memory transfers up to 3D, so we
                // can deal with the 2 innermost strides with OpenCL.
                uint64_t off = z * c.stride_bytes[2] + w * c.stride_bytes[3];

                size_t offset[3] = { off, 0, 0 };
                size_t region[3] = { c.chunk_size, c.extent[0], c.extent[1] };

                DEBUG_PRINTF( user_context, "    clEnqueueReadBufferRect ((%d, %d), (%p -> %p) + %d, %dx%dx%d bytes, %dx%d)\n",
                              z, w,
                              (void *)c.src, c.dst, (int)off,
                              (int)region[0], (int)region[1], (int)region[2],
                              (int)c.stride_bytes[0], (int)c.stride_bytes[1]);

                cl_int err = clEnqueueReadBufferRect(ctx.cmd_queue, (cl_mem)c.src, CL_FALSE,
                                                     offset, offset, region,
                                                     c.stride_bytes[0], c.stride_bytes[1],
                                                     c.stride_bytes[0], c.stride_bytes[1],
                                                     (void *)c.dst,
                                                     0, NULL, NULL);

                if (err != CL_SUCCESS) {
                    halide_error_varargs(user_context, "CL: clEnqueueReadBufferRect failed (%s)\n",
                                         get_opencl_error_name(err));
                    return err;
                }
#else
                for (int y = 0; y < c.extent[1]; y++) {
                    for (int x = 0; x < c.extent[0]; x++) {
                        uint64_t off = (x * c.stride_bytes[0] +
                                        y * c.stride_bytes[1] +
                                        z * c.stride_bytes[2] +
                                        w * c.stride_bytes[3]);
                        void *src = (void *)(c.src + off);
                        void *dst = (void *)(c.dst + off);
                        uint64_t size = c.chunk_size;

                        DEBUG_PRINTF( user_context, "    clEnqueueReadBuffer ((%d, %d, %d, %d), %lld bytes, %p -> %p)\n",
                                      x, y, z, w,
                                      (long long)size, (void *)src, dst );

                        cl_int err = clEnqueueReadBuffer(ctx.cmd_queue, (cl_mem)c.src,
                                                         CL_FALSE, off, size, dst, 0, NULL, NULL);
                        if (err != CL_SUCCESS) {
                            halide_error_varargs(user_context, "CL: clEnqueueReadBuffer failed (%s)\n",
                                                 get_opencl_error_name(err));
                            return err;
                        }
                    }
                }
#endif
            }
        }
        // The writes above are all non-blocking, so empty the command
        // queue before we proceed so that other host code won't read
        // bad data.
        clFinish(ctx.cmd_queue);

        #ifdef DEBUG
        uint64_t t_after = halide_current_time_ns(user_context);
        halide_printf(user_context, "    Time: %f ms\n", (t_after - t_before) / 1.0e6);
        #endif
    }
    buf->dev_dirty = false;
    return 0;
}

WEAK int halide_dev_run(void *user_context,
                        void *state_ptr,
                        const char* entry_name,
                        int blocksX, int blocksY, int blocksZ,
                        int threadsX, int threadsY, int threadsZ,
                        int shared_mem_bytes,
                        size_t arg_sizes[],
                        void* args[]) {
    DEBUG_PRINTF( user_context, "CL: halide_dev_run (user_context: %p, entry: %s, blocks: %dx%dx%d, threads: %dx%dx%d, shmem: %d)\n",
                  user_context, entry_name,
                  blocksX, blocksY, blocksZ,
                  threadsX, threadsY, threadsZ,
                  shared_mem_bytes );

    cl_int err;
    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    // Create kernel object for entry_name from the program for this module.
    halide_assert(user_context, state_ptr);
    cl_program program = ((module_state*)state_ptr)->program;

    halide_assert(user_context, program);
    DEBUG_PRINTF( user_context, "    clCreateKernel %s -> ", entry_name );
    cl_kernel f = clCreateKernel(program, entry_name, &err);
    if (err != CL_SUCCESS) {
        DEBUG_PRINTF( user_context, "%s\n", get_opencl_error_name(err) );
        halide_error_varargs(user_context, "CL: clCreateKernel (%s) failed (%s)\n",
                             entry_name, get_opencl_error_name(err));
        return err;
    } else {
        #ifdef DEBUG
        uint64_t t_create_kernel = halide_current_time_ns(user_context);
        halide_printf( user_context, "%p (%f ms)\n",
                       f, (t_create_kernel - t_before) / 1.0e6 );
        #endif
    }

    // Pack dims
    size_t global_dim[3] = {blocksX*threadsX,  blocksY*threadsY,  blocksZ*threadsZ};
    size_t local_dim[3] = {threadsX, threadsY, threadsZ};

    // Set args
    int i = 0;
    while (arg_sizes[i] != 0) {
        DEBUG_PRINTF(user_context, "    clSetKernelArg %i %i [0x%x ...]\n", i, arg_sizes[i], *(int *)args[i]);
        cl_int err = clSetKernelArg(f, i, arg_sizes[i], args[i]);
        if (err != CL_SUCCESS) {
            halide_error_varargs(user_context, "CL: clSetKernelArg failed (%s)\n",
                                 get_opencl_error_name(err));
            return err;
        }
        i++;
    }
    // Set the shared mem buffer last
    // Always set at least 1 byte of shmem, to keep the launch happy
    DEBUG_PRINTF(user_context, "    clSetKernelArg %i %i [NULL]\n", i, shared_mem_bytes);
    err = clSetKernelArg(f, i, (shared_mem_bytes > 0) ? shared_mem_bytes : 1, NULL);
    if (err != CL_SUCCESS) {
        halide_error_varargs(user_context, "CL: clSetKernelArg failed (%s)\n",
                             get_opencl_error_name(err));
        return err;
    }

    // Launch kernel
    DEBUG_PRINTF( user_context, "    clEnqueueNDRangeKernel %dx%dx%d, %dx%dx%d -> ",
                  blocksX, blocksY, blocksZ,
                  threadsX, threadsY, threadsZ );
    err = clEnqueueNDRangeKernel(ctx.cmd_queue, f,
                                 // NDRange
                                 3, NULL, global_dim, local_dim,
                                 // Events
                                 0, NULL, NULL);
    DEBUG_PRINTF( user_context, "%s\n", get_opencl_error_name(err) );
    if (err != CL_SUCCESS) {
        halide_error_varargs(user_context, "CL: clEnqueueNDRangeKernel failed (%s)\n", get_opencl_error_name(err));
        return err;
    }

    DEBUG_PRINTF( user_context, "    clReleaseKernel %p\n", f );
    clReleaseKernel(f);

    #ifdef DEBUG
    err = clFinish(ctx.cmd_queue);
    if (err != CL_SUCCESS) {
        halide_error_varargs(user_context, "CL: clFinish failed (%d)\n", err);
        return err;
    }
    uint64_t t_after = halide_current_time_ns(user_context);
    halide_printf(user_context, "    Time: %f ms\n", (t_after - t_before) / 1.0e6);
    #endif
    return 0;
}

} // extern "C" linkage

namespace Halide { namespace Runtime { namespace Internal {
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
    default: return "<Unknown error>";
    }
}

}}} // namespace Halide::Runtime::Internal
