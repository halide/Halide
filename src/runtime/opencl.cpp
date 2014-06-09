#include "mini_stdint.h"
#include "scoped_spin_lock.h"
#include "../buffer_t.h"
#include "HalideRuntime.h"

#include "mini_cl.h"

#ifdef DEBUG
#define DEBUG_PRINTF halide_printf
#else
// This ensures that DEBUG and non-DEBUG have the same semicolon eating behavior.
static void __noop_printf(void *, const char *, ...) { }
#define DEBUG_PRINTF __noop_printf
#endif

extern "C" {

extern int64_t halide_current_time_ns(void *user_context);
extern void free(void *);
extern void *malloc(size_t);
extern int snprintf(char *, size_t, const char *, ...);
extern char *getenv(const char *);
extern const char * strstr(const char *, const char *);

}

extern "C" {
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

WEAK void halide_set_cl_context(cl_context* ctx_ptr, cl_command_queue* q_ptr, volatile int* lock_ptr) {
    cl_ctx_ptr = ctx_ptr;
    cl_q_ptr = q_ptr;
    cl_lock_ptr = lock_ptr;
}

static int create_context(void *user_context, cl_context *ctx, cl_command_queue *q);

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
        cl_int error = create_context(user_context, cl_ctx_ptr, cl_q_ptr);
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

}

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

extern "C" {

// Structure to hold the state of a module attached to the context.
// Also used as a linked-list to keep track of all the different
// modules that are attached to a context in order to release them all
// when then context is released.
typedef struct _module_state_ {
    cl_program program;
    _module_state_ *next;
} module_state;
module_state WEAK *state_list = NULL;

WEAK bool halide_validate_dev_pointer(void *user_context, buffer_t* buf, size_t size=0) {
    if (buf->dev == 0) {
        return true;
    }

    size_t real_size;
    cl_int result = clGetMemObjectInfo((cl_mem)buf->dev, CL_MEM_SIZE, sizeof(size_t), &real_size, NULL);
    if (result != CL_SUCCESS) {
        halide_printf(user_context, "CL: Bad device pointer %p: clGetMemObjectInfo returned %d\n",
                      (void *)buf->dev, result);
        return false;
    }

    DEBUG_PRINTF( user_context, "CL: validate %p: asked for %lld, actual allocated %lld\n",
                  (void*)buf->dev, (long long)size, (long long)real_size );

    if (size) halide_assert(user_context, real_size >= size && "Validating pointer with insufficient size");
    return true;
}

WEAK int halide_dev_free(void *user_context, buffer_t* buf) {
    DEBUG_PRINTF( user_context, "CL: halide_dev_free (user_context: %p, buf: %p)\n", user_context, buf );

    ClContext ctx(user_context);

    // halide_dev_free, at present, can be exposed to clients and they
    // should be allowed to call halide_dev_free on any buffer_t
    // including ones that have never been used with a GPU.
    if (buf->dev == 0) {
      return 0;
    }

    #ifdef DEBUG
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, halide_validate_dev_pointer(user_context, buf));
    cl_int result = clReleaseMemObject((cl_mem)buf->dev);
    // If clReleaseMemObject fails, it is unlikely to succeed in a later call, so
    // we just end our reference to it regardless.
    buf->dev = 0;
    if (result != CL_SUCCESS) {
        halide_error_varargs(user_context, "CL: clReleaseMemObject failed (%d)", result);
        return result;
    }

    #ifdef DEBUG
    uint64_t t_after = halide_current_time_ns(user_context);
    halide_printf(user_context, "    Time: %f ms\n", (t_after - t_before) / 1.0e6);
    #endif

    return 0;
}

// Initializes the context used by the default implementation
// of halide_acquire_context.
static int create_context(void *user_context, cl_context *ctx, cl_command_queue *q) {
    DEBUG_PRINTF( user_context, "    create_context (user_context: %p)\n", user_context );

    halide_assert(user_context, ctx != NULL && *ctx == NULL);
    halide_assert(user_context, q != NULL && *q == NULL);

    cl_int err = 0;

    const cl_uint maxPlatforms = 4;
    cl_platform_id platforms[maxPlatforms];
    cl_uint platformCount = 0;

    err = clGetPlatformIDs( maxPlatforms, platforms, &platformCount );
    if (err != CL_SUCCESS) {
        halide_error_varargs(user_context, "CL: clGetPlatformIDs failed (%d)\n", err);
        return err;
    }

    cl_platform_id platform = NULL;

    // Find the requested platform, or the first if none specified.
    const char * name = getenv("HL_OCL_PLATFORM");
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
        halide_printf(user_context, "    clGetPlatformInfo(CL_PLATFORM_NAME) failed (%d)\n", err);
        // This is just debug info, report the error but don't fail context creation due to it.
        //return err;
    } else {
        halide_printf(user_context, "    Got platform '%s', about to create context (t=%lld)\n",
                      platformName, (long long)halide_current_time_ns(user_context));
    }
    #endif

    cl_device_type device_type = 0;
    // Find the device types requested.
    const char * dev_type = getenv("HL_OCL_DEVICE");
    if (dev_type != NULL) {
        if (strstr("cpu", dev_type)) {
            device_type |= CL_DEVICE_TYPE_CPU;
        }
        if (strstr("gpu", dev_type)) {
            device_type |= CL_DEVICE_TYPE_GPU;
        }
    }
    // If no devices are specified yet, just use all.
    if (device_type == 0) {
        device_type = CL_DEVICE_TYPE_ALL;
    }

    // Make sure we have a device
    const cl_uint maxDevices = 4;
    cl_device_id devices[maxDevices];
    cl_uint deviceCount = 0;
    err = clGetDeviceIDs( platform, device_type, maxDevices, devices, &deviceCount );
    if (err != CL_SUCCESS) {
        halide_error_varargs(user_context, "CL: clGetDeviceIDs failed (%d)\n", err);
        return err;
    }
    if (deviceCount == 0) {
        halide_error(user_context, "CL: Failed to get device\n");
        return CL_DEVICE_NOT_FOUND;
    }

    cl_device_id dev = devices[deviceCount-1];

    #ifdef DEBUG
    const cl_uint maxDeviceName = 256;
    char deviceName[maxDeviceName];
    err = clGetDeviceInfo( dev, CL_DEVICE_NAME, maxDeviceName, deviceName, NULL );
    if (err != CL_SUCCESS) {
        halide_printf(user_context, "    clGetDeviceInfo(CL_DEVICE_NAME) failed (%d)\n", err);
        // This is just debug info, report the error but don't fail context create if it fails.
        //return err;
    } else {
        halide_printf(user_context, "    Got device '%s'\n", deviceName);
    }
    #endif


    // Create context and command queue.
    cl_context_properties properties[] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0 };
    *ctx = clCreateContext(properties, 1, &dev, NULL, NULL, &err);
    if (err != CL_SUCCESS) {
        halide_error_varargs(user_context, "CL: clCreateContext failed (%d)\n", err);
        return err;
    }
    DEBUG_PRINTF( user_context, "    Created context %p\n", *ctx );

    *q = clCreateCommandQueue(*ctx, dev, 0, &err);
    if (err != CL_SUCCESS) {
        halide_error_varargs(user_context, "CL: clCreateCommandQueue failed (%d)\n", err);
        return err;
    }
    DEBUG_PRINTF( user_context, "    Created command queue %p\n", *q );

    return err;
}

WEAK void* halide_init_kernels(void *user_context, void *state_ptr, const char* src, int size) {
    DEBUG_PRINTF( user_context, "CL: halide_init_kernels (user_context: %p, state_ptr: %p, program: %p, %i)\n",
                  user_context, state_ptr, src, size );

    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return NULL;
    }

    #ifdef DEBUG
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    // Create the module state if necessary
    module_state *state = (module_state*)state_ptr;
    if (!state) {
        state = (module_state*)malloc(sizeof(module_state));
        state->program = NULL;
        state->next = state_list;
        state_list = state;
    }

    // Create the program if necessary.
    if (!state->program && size > 1) {
        cl_int err = 0;
        cl_device_id dev;

        err = clGetContextInfo(ctx.context, CL_CONTEXT_DEVICES, sizeof(dev), &dev, NULL);
        if (err != CL_SUCCESS) {
            halide_error_varargs(user_context, "CL: clGetContextInfo(CL_CONTEXT_DEVICES) failed (%d)\n", err);
            return NULL;
        }

        cl_device_id devices[] = { dev };
        size_t lengths[] = { size };
        const char *build_options = NULL;

        // Program is OpenCL C.
        DEBUG_PRINTF(user_context, "    Compiling OpenCL C kernel: (%i chars)\n", size);

        const char * sources[] = { src };
        state->program = clCreateProgramWithSource(ctx.context, 1, &sources[0], NULL, &err );
        if (err != CL_SUCCESS) {
            halide_error_varargs(user_context, "CL: clCreateProgramWithSource failed (%d)\n", err);
            return NULL;
        }

        err = clBuildProgram(state->program, 1, &dev, build_options, NULL, NULL );
        if (err != CL_SUCCESS) {
            halide_error_varargs(user_context, "CL: clBuildProgram failed (%d)\n", err);

            // Allocate an appropriately sized buffer for the build log.
            size_t len = 0;
            char *buffer = NULL;
            if (clGetProgramBuildInfo(state->program, dev, CL_PROGRAM_BUILD_LOG, 0, NULL, &len) == CL_SUCCESS) {
                buffer = (char*)malloc((++len)*sizeof(char));
            }

            // Get build log
            if (buffer && clGetProgramBuildInfo(state->program, dev, CL_PROGRAM_BUILD_LOG, len, buffer, NULL) == CL_SUCCESS) {
                halide_printf(user_context, "    Build Log:\n %s\n-----\n", buffer);
            } else {
                halide_printf(user_context, "    clGetProgramBuildInfo failed\n");
            }

            if (buffer) {
                free(buffer);
            }

            halide_assert(user_context, err == CL_SUCCESS);
            return NULL;
        }
    }

    #ifdef DEBUG
    uint64_t t_after = halide_current_time_ns(user_context);
    halide_printf(user_context, "    Time: %f ms\n", (t_after - t_before) / 1.0e6);
    #endif

    return state;
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
        halide_error_varargs(user_context, "CL: clFinish failed (%d)\n", err);
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

    // Unload the modules attached to this context
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
    state_list = NULL;

    // Release the context itself, if we created it.
    if (ctx == weak_cl_ctx) {
        err = clReleaseCommandQueue(weak_cl_q);
        halide_assert(user_context, err == CL_SUCCESS);
        weak_cl_q = NULL;

        DEBUG_PRINTF(user_context, "    clReleaseContext %p\n", weak_cl_ctx);
        err = clReleaseContext(weak_cl_ctx);
        halide_assert(user_context, err == CL_SUCCESS);
        weak_cl_ctx = NULL;
    }

    halide_release_cl_context(user_context);
}

static size_t __buf_size(void *user_context, buffer_t* buf) {
    size_t size = 0;
    for (int i = 0; i < sizeof(buf->stride) / sizeof(buf->stride[0]); i++) {
        size_t total_dim_size = buf->elem_size * buf->extent[i] * buf->stride[i];
        if (total_dim_size > size)
            size = total_dim_size;
    }
    halide_assert(user_context, size);
    return size;
}

WEAK int halide_dev_malloc(void *user_context, buffer_t* buf) {
    DEBUG_PRINTF( user_context, "CL: halide_dev_malloc (user_context: %p, buf: %p)\n", user_context, buf );

    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    size_t size = __buf_size(user_context, buf);
    if (buf->dev) {
        halide_assert(user_context, halide_validate_dev_pointer(user_context, buf, size));
        return 0;
    }

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
    buf->dev = (uint64_t)clCreateBuffer(ctx.context, CL_MEM_READ_WRITE, size, NULL, &err);
    if (err != CL_SUCCESS || buf->dev == 0) {
        halide_error_varargs(user_context, "CL: clCreateBuffer failed (%d)\n", err);
        return err;
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
        size_t size = __buf_size(user_context, buf);
        halide_assert(user_context, halide_validate_dev_pointer(user_context, buf, size));
        DEBUG_PRINTF( user_context, "    copy_to_dev (%lld bytes, %p -> %p)\n",
                      (long long)size, buf->host, (void *)buf->dev );
        cl_int err = clEnqueueWriteBuffer(ctx.cmd_queue, (cl_mem)((void*)buf->dev),
                                          CL_TRUE, 0, size, buf->host, 0, NULL, NULL);
        if (err != CL_SUCCESS) {
            halide_error_varargs(user_context, "CL: clEnqueueWriteBuffer failed (%d)\n", err);
            return err;
        }

        #ifdef DEBUG
        uint64_t t_after = halide_current_time_ns(user_context);
        halide_printf(user_context, "    Time: %f ms\n", (t_after - t_before) / 1.0e6);
        #endif
    }
    buf->host_dirty = false;
    return 0;
}

WEAK int halide_copy_to_host(void *user_context, buffer_t* buf) {
    DEBUG_PRINTF(user_context, "CL: halide_copy_to_host (user_context: %p, buf: %p)\n", user_context, buf );

    // Acquire the context so we can use the command queue. This also avoids multiple
    // redundant calls to clEnqueueReadBuffer when multiple threads are trying to copy
    // the same buffer.
    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    if (buf->dev_dirty) {
        #ifdef DEBUG
        uint64_t t_before = halide_current_time_ns(user_context);
        #endif

        halide_assert(user_context, buf->host && buf->dev);
        size_t size = __buf_size(user_context, buf);
        halide_assert(user_context, halide_validate_dev_pointer(user_context, buf, size));
        DEBUG_PRINTF( user_context, "    copy_to_host (%lld bytes, %p -> %p)\n",
                      (long long)size, (void *)buf->dev, buf );

        cl_int err = clEnqueueReadBuffer(ctx.cmd_queue, (cl_mem)((void*)buf->dev),
                                         CL_TRUE, 0, size, buf->host, 0, NULL, NULL);
        if (err != CL_SUCCESS) {
            halide_error_varargs(user_context, "CL: clEnqueueReadBuffer failed (%d)\n", err);
            return err;
        }

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
    cl_kernel f = clCreateKernel(program, entry_name, &err);
    if (err != CL_SUCCESS) {
        halide_error_varargs(user_context, "CL: clCreateKernel (%s) failed (%d)\n", entry_name, err);
        return err;
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
            halide_error_varargs(user_context, "CL: clSetKernelArg failed (%d)\n", err);
            return err;
        }
        i++;
    }
    // Set the shared mem buffer last
    // Always set at least 1 byte of shmem, to keep the launch happy
    DEBUG_PRINTF(user_context, "    clSetKernelArg %i %i [NULL]\n", i, shared_mem_bytes);
    err = clSetKernelArg(f, i, (shared_mem_bytes > 0) ? shared_mem_bytes : 1, NULL);
    if (err != CL_SUCCESS) {
        halide_error_varargs(user_context, "CL: clSetKernelArg failed (%d)\n", err);
        return err;
    }

    // Launch kernel
    err = clEnqueueNDRangeKernel(ctx.cmd_queue, f,
                                 // NDRange
                                 3, NULL, global_dim, local_dim,
                                 // Events
                                 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        halide_error_varargs(user_context, "CL: clEnqueueNDRangeKernel failed (%d)\n", err);
        return err;
    }

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

