#include "mini_stdint.h"
#include "scoped_spin_lock.h"
#include "../buffer_t.h"
#include "HalideRuntime.h"

#ifdef _WIN32
#define CL_API_ENTRY
#define CL_API_CALL     __stdcall
#define CL_CALLBACK     __stdcall
#else
#define CL_API_ENTRY
#define CL_API_CALL
#define CL_CALLBACK
#endif
#define CL_API_SUFFIX__VERSION_1_0
#define CL_API_SUFFIX__VERSION_1_1
#define CL_API_SUFFIX__VERSION_1_2
#define CL_EXT_SUFFIX__VERSION_1_0_DEPRECATED
#define CL_EXT_PREFIX__VERSION_1_0_DEPRECATED
#define CL_EXT_SUFFIX__VERSION_1_1_DEPRECATED
#define CL_EXT_PREFIX__VERSION_1_1_DEPRECATED
#define CL_EXT_SUFFIX__VERSION_1_2_DEPRECATED
#define CL_EXT_PREFIX__VERSION_1_2_DEPRECATED
typedef int32_t cl_int;
typedef uint32_t cl_uint;
typedef int64_t cl_long;
typedef uint64_t cl_ulong;

#include "CL/cl.h"

extern "C" {

extern int64_t halide_current_time_ns(void *user_context);
extern void free(void *);
extern void *malloc(size_t);
extern int snprintf(char *, size_t, const char *, ...);
extern char *getenv(const char *);
extern const char * strstr(const char *, const char *);

}

#ifndef DEBUG
#define CHECK_ERR(e,str)                                  \
    do {                                                  \
        if (err != CL_SUCCESS) {                          \
            halide_error_varargs(user_context, str ": %d", e);  \
            return err;                                   \
        }                                                 \
    } while (0) /* eat semicolon */
#else // DEBUG
#define CHECK_ERR(err,str)                                \
    do {                                                  \
        if (err != CL_SUCCESS) {                          \
            halide_printf(user_context, "CL: %s returned non-success at line %d: %d\n", str, __LINE__, err); \
            halide_assert(user_context, err == CL_SUCCESS); \
            return err;                                   \
        }                                                 \
    } while (0) /* eat semicolon */
#endif //DEBUG

#define CHECK_CALL(c,str)                                 \
  do {                                                    \
    int err = (c);                                        \
    CHECK_ERR(err, str);                                  \
  } while (0) /* eat semicolon */

extern "C" {
// An OpenCL context/queue defined in this module with weak linkage
cl_context WEAK weak_cl_ctx = 0;
cl_command_queue WEAK weak_cl_q = 0;

// These are pointers to the real context/queue stored elsewhere.
static cl_context* cl_ctx = NULL;
static cl_command_queue* cl_q = NULL;

// A single global context lock for OpenCL.
volatile int WEAK weak_cl_lock = 0;
static volatile int *cl_lock = NULL;

// Structure to hold the state of a module attached to the context.
// Also used as a linked-list to keep track of all the different
// modules that are attached to a context in order to release them all
// when then context is released.
struct _module_state_ WEAK *state_list = NULL;
typedef struct _module_state_ {
    cl_program program;
    _module_state_ *next;
} module_state;

WEAK void halide_set_cl_context(cl_context* ctx, cl_command_queue* q, volatile int* lock) {
    cl_ctx = ctx;
    cl_q = q;
    cl_lock = lock;
}

WEAK bool halide_validate_dev_pointer(void *user_context, buffer_t* buf, size_t size=0) {
    if (buf->dev == 0)
        return true;

    size_t real_size;
    cl_int result = clGetMemObjectInfo((cl_mem)buf->dev, CL_MEM_SIZE, sizeof(size_t), &real_size, NULL);
    if (result) {
        halide_printf(user_context, "Bad device pointer %p: clGetMemObjectInfo returned %d\n",
                      (void *)buf->dev, result);
        return false;
    }
    #ifdef DEBUG
    halide_printf(user_context, "validate %p: asked for %lld, actual allocated %lld\n",
                  (void*)buf->dev, (long long)size, (long long)real_size);
    #endif
    if (size) halide_assert(user_context, real_size >= size && "Validating pointer with insufficient size");
    return true;
}

WEAK int halide_dev_free(void *user_context, buffer_t* buf) {
    ScopedSpinLock l(cl_lock);

    // halide_dev_free, at present, can be exposed to clients and they
    // should be allowed to call halide_dev_free on any buffer_t
    // including ones that have never been used with a GPU.
    if (buf->dev == 0)
      return 0;

    #ifdef DEBUG
    halide_printf(user_context, "In dev_free of %p - dev: 0x%p\n", buf, (void*)buf->dev);
    #endif

    halide_assert(user_context, halide_validate_dev_pointer(user_context, buf));
    CHECK_CALL( clReleaseMemObject((cl_mem)buf->dev), "clReleaseMemObject" );
    buf->dev = 0;
    return 0;
}

static int init_context(void *user_context) {
    cl_int err = 0;

    // Initialize one shared context for all Halide compiled instances
    if (*cl_ctx == NULL) {
        #ifdef DEBUG
        halide_printf(user_context, "CL: Creating context\n");
        #endif
        const cl_uint maxPlatforms = 4;
        cl_platform_id platforms[maxPlatforms];
        cl_uint platformCount = 0;

        err = clGetPlatformIDs( maxPlatforms, platforms, &platformCount );
        CHECK_ERR( err, "clGetPlatformIDs" );

        cl_platform_id platform = NULL;

        // Find the requested platform, or the first if none specified.
        const char * name = getenv("HL_OCL_PLATFORM");
        if (name != NULL) {
            for (cl_uint i = 0; i < platformCount; ++i) {
                const cl_uint maxPlatformName = 256;
                char platformName[maxPlatformName];
                err = clGetPlatformInfo( platforms[i], CL_PLATFORM_NAME, maxPlatformName, platformName, NULL );
                if (err != CL_SUCCESS) continue;

                #ifdef DEBUG
                halide_printf(user_context, "Found platform '%s'\n", platformName);
                #endif

                if (strstr(platformName, name))
                {
                    platform = platforms[i];
                    break;
                }
            }
        } else if (platformCount > 0) {
            platform = platforms[0];
        }
        if (platform == NULL){
            halide_printf(user_context, "Failed to find OpenCL platform\n");
            return -1;
        }

        #ifdef DEBUG
        const cl_uint maxPlatformName = 256;
        char platformName[maxPlatformName];
        err = clGetPlatformInfo( platform, CL_PLATFORM_NAME, maxPlatformName, platformName, NULL );
        CHECK_ERR( err, "clGetPlatformInfo" );

        halide_printf(user_context, "Got platform '%s', about to create context (t=%lld)\n",
                      platformName, (long long)halide_current_time_ns(user_context));
        #endif

        cl_device_type device_type = 0;
        // Find the device types requested.
        const char * dev_type = getenv("HL_OCL_DEVICE");
        if (dev_type != NULL) {
            if (strstr("cpu", dev_type))
                device_type |= CL_DEVICE_TYPE_CPU;
            if (strstr("gpu", dev_type))
                device_type |= CL_DEVICE_TYPE_GPU;
        }
        // If no devices are specified yet, just use all.
        if (device_type == 0)
            device_type = CL_DEVICE_TYPE_ALL;

        // Make sure we have a device
        const cl_uint maxDevices = 4;
        cl_device_id devices[maxDevices];
        cl_uint deviceCount = 0;
        err = clGetDeviceIDs( platform, device_type, maxDevices, devices, &deviceCount );
        CHECK_ERR( err, "clGetDeviceIDs" );
        if (deviceCount == 0) {
            halide_printf(user_context, "Failed to get device\n");
            return -1;
        }

        cl_device_id dev = devices[deviceCount-1];

        #ifdef DEBUG
        const cl_uint maxDeviceName = 256;
        char deviceName[maxDeviceName];
        err = clGetDeviceInfo( dev, CL_DEVICE_NAME, maxDeviceName, deviceName, NULL );
        CHECK_ERR( err, "clGetDeviceInfo" );

        halide_printf(user_context, "Got device '%s', about to create context (t=%lld)\n",
                      deviceName, (long long)halide_current_time_ns(user_context));
        #endif


        // Create context
        cl_context_properties properties[] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0 };
        *cl_ctx = clCreateContext(properties, 1, &dev, NULL, NULL, &err);
        CHECK_ERR( err, "clCreateContext" );
        // cuEventCreate(&__start, 0);
        // cuEventCreate(&__end, 0);

        halide_assert(user_context, !(*cl_q));
        *cl_q = clCreateCommandQueue(*cl_ctx, dev, 0, &err);
        CHECK_ERR( err, "clCreateCommandQueue" );
    } else {
        #ifdef DEBUG
        halide_printf(user_context, "Already had context %p\n", *cl_ctx);
        #endif
    }
    return err;
}

static int create_and_build_program(void *user_context, module_state *state, const char *src, int size) {
    cl_int err = 0;
    cl_device_id dev;

    CHECK_CALL( clGetContextInfo(*cl_ctx, CL_CONTEXT_DEVICES, sizeof(dev), &dev, NULL), "clGetContextInfo" );

    // Initialize a module for just this Halide module
    if ((!state->program) && (size > 1)) {
        // Create module

        cl_device_id devices[] = { dev };
        size_t lengths[] = { size };
        const char *build_options = NULL;

        if (strstr(src, "/*OpenCL C*/")) {
            // Program is OpenCL C.

            #ifdef DEBUG
            halide_printf(user_context, "Compiling OpenCL C kernel: %s\n\n", src);
            #endif

            const char * sources[] = { src };
            state->program = clCreateProgramWithSource(*cl_ctx, 1, &sources[0], NULL, &err );
            CHECK_ERR( err, "clCreateProgramWithSource" );
        } else {
            // Program is SPIR binary.

            #ifdef DEBUG
            halide_printf(user_context, "Compiling SPIR kernel (%i bytes)\n", size);
            #endif

            const unsigned char * binaries[] = { (unsigned char *)src };
            state->program = clCreateProgramWithBinary(*cl_ctx, 1, devices, lengths, &binaries[0], NULL, &err );
            CHECK_ERR( err, "clCreateProgramWithBinary" );

            build_options = "-x spir";
        }

        err = clBuildProgram(state->program, 1, &dev, build_options, NULL, NULL );
        if (err != CL_SUCCESS) {
            size_t len = 0;
            char *buffer = NULL;

            halide_printf(user_context, "Error: Failed to build program executable! err = %d\n", err);

            // Get size of build log
            if (clGetProgramBuildInfo(state->program, dev, CL_PROGRAM_BUILD_LOG, 0, NULL, &len) == CL_SUCCESS)
                buffer = (char*)malloc((++len)*sizeof(char));

            // Get build log
            if (buffer && clGetProgramBuildInfo(state->program, dev, CL_PROGRAM_BUILD_LOG, len, buffer, NULL) == CL_SUCCESS)
                halide_printf(user_context, "Build Log:\n %s\n-----\n", buffer);
            else
                halide_printf(user_context, "clGetProgramBuildInfo failed to get build log!\n");

            if (buffer)
                free(buffer);

            halide_assert(user_context, err == CL_SUCCESS);
        }
    }
    return err;
}

WEAK void* halide_init_kernels(void *user_context, void *state_ptr, const char* src, int size) {
    // If the context hasn't been set externally, use the weak context.
    if (!cl_ctx) {
        cl_ctx = &weak_cl_ctx;
        cl_q = &weak_cl_q;
        cl_lock = &weak_cl_lock;
    }

    // Do this after setting the lock.
    ScopedSpinLock lock(cl_lock);

    cl_int err = 0;
    err = init_context(user_context);
    if (err != CL_SUCCESS) {
        return NULL;
    }

    // Create the module state if necessary
    module_state *state = (module_state*)state_ptr;
    if (!state) {
        state = (module_state*)malloc(sizeof(module_state));
        state->program = NULL;
        state->next = state_list;
        state_list = state;
    }

    err = create_and_build_program(user_context, state, src, size);
    if (err != CL_SUCCESS) {
        return NULL;
    }

    return state;
}

// Used to generate correct timings when tracing
WEAK void halide_dev_sync(void *user_context) {
    ScopedSpinLock l(cl_lock);

    clFinish(*cl_q);
}

WEAK void halide_release(void *user_context) {
    ScopedSpinLock l(cl_lock);

    #ifdef DEBUG
    halide_printf(user_context, "CL: halide_release\n");
    #endif

    // Only perform release if a context has actually been created.
    if (cl_ctx) {
        // TODO: this is for timing; bad for release-mode performance
        #ifdef DEBUG
        halide_printf(user_context, "dev_sync on exit\n" );
        #endif
        clFinish(*cl_q);

        // Unload the modules attached to this context
        module_state *state = state_list;
        while (state) {
            if (state->program) {
                #ifdef DEBUG
                halide_printf(user_context, "clReleaseProgram %p\n", state->program);
                #endif

                clReleaseProgram(state->program);
                state->program = NULL;
            }
            state = state->next;
        }
        state_list = NULL;

        // Only release the context if we own it.
        if (weak_cl_ctx) {
            clReleaseCommandQueue(weak_cl_q);
            #ifdef DEBUG
            halide_printf(user_context, "clReleaseContext %p\n", weak_cl_ctx);
            #endif
            clReleaseContext(weak_cl_ctx);

            weak_cl_ctx = NULL;
            weak_cl_q = NULL;
        }

        cl_ctx = NULL;
        cl_q = NULL;
        cl_lock = NULL;
    }
}

static cl_kernel __get_kernel(void *user_context, cl_program program, const char* entry_name) {
    cl_kernel f;
    #ifdef DEBUG
    halide_printf(user_context, "get_kernel %s\n", entry_name);
    #endif
    // Get kernel function ptr
    int err;
    f = clCreateKernel(program, entry_name, &err);
    if (err != CL_SUCCESS) {
        halide_error(user_context, "clCreateKernel failed");
        return NULL;
    }

    return f;
}

static cl_mem __dev_malloc(void *user_context, size_t bytes) {
    cl_mem p;
    #ifdef DEBUG
    halide_printf(user_context, "dev_malloc (%lld bytes)\n", (long long)bytes);
    #endif

    int err;
    p = clCreateBuffer(*cl_ctx, CL_MEM_READ_WRITE, bytes, NULL, &err );
    #ifdef DEBUG
    halide_printf(user_context, "    returned: %p (err: %d)\n", (void*)p, err);
    #endif
    halide_assert(user_context, p);
    return p;
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
    ScopedSpinLock l(cl_lock);

    if (buf->dev) {
        halide_assert(user_context, halide_validate_dev_pointer(user_context, buf));
        return 0;
    }

    size_t size = __buf_size(user_context, buf);
    #ifdef DEBUG
    halide_printf(user_context, "dev_malloc allocating buffer of %lld bytes, "
                  "extents: %lldx%lldx%lldx%lld strides: %lldx%lldx%lldx%lld (%d bytes per element)\n",
                  (long long)size, (long long)buf->extent[0], (long long)buf->extent[1],
                  (long long)buf->extent[2], (long long)buf->extent[3],
                  (long long)buf->stride[0], (long long)buf->stride[1],
                  (long long)buf->stride[2], (long long)buf->stride[3],
                  buf->elem_size);
    #endif

    buf->dev = (uint64_t)__dev_malloc(user_context, size);
    #ifdef DEBUG
    halide_printf(user_context, "dev_malloc allocated buffer %p of with buf->dev of %p\n",
                  buf, (void *)buf->dev);
    #endif
    if (!buf->dev) {
        halide_error(user_context, "clCreateBuffer failed\n");
        return 1;
    }
    return 0;
}

WEAK int halide_copy_to_dev(void *user_context, buffer_t* buf) {
    ScopedSpinLock l(cl_lock);

    if (buf->host_dirty) {
        halide_assert(user_context, buf->host && buf->dev);
        size_t size = __buf_size(user_context, buf);
        #ifdef DEBUG
        halide_printf(user_context, "copy_to_dev (%lld bytes) %p -> %p\n", (long long)size, buf->host, (void*)buf->dev);
        #endif
        halide_assert(user_context, halide_validate_dev_pointer(user_context, buf));
        int err = clEnqueueWriteBuffer( *cl_q, (cl_mem)((void*)buf->dev), CL_TRUE, 0, size, buf->host, 0, NULL, NULL );
        CHECK_ERR( err, "clEnqueueWriteBuffer" );
    }
    buf->host_dirty = false;
    return 0;
}

WEAK int halide_copy_to_host(void *user_context, buffer_t* buf) {
    ScopedSpinLock l(cl_lock);

    if (buf->dev_dirty) {
        clFinish(*cl_q); // block on completion before read back
        halide_assert(user_context, buf->host && buf->dev);
        size_t size = __buf_size(user_context, buf);
        #ifdef DEBUG
        halide_printf(user_context, "copy_to_host buf %p (%lld bytes) %p -> %p\n", buf, (long long)size,
                      (void*)buf->dev, buf->host );
        #endif

        halide_assert(user_context, halide_validate_dev_pointer(user_context, buf, size));
        int err = clEnqueueReadBuffer( *cl_q, (cl_mem)((void*)buf->dev), CL_TRUE, 0, size, buf->host, 0, NULL, NULL );
        CHECK_ERR( err, "clEnqueueReadBuffer" );
    }
    buf->dev_dirty = false;
    return 0;
}

WEAK int halide_dev_run(
    void *user_context,
    void *state_ptr,
    const char* entry_name,
    int blocksX, int blocksY, int blocksZ,
    int threadsX, int threadsY, int threadsZ,
    int shared_mem_bytes,
    size_t arg_sizes[],
    void* args[])
{
    ScopedSpinLock l(cl_lock);

    halide_assert(user_context, state_ptr);
    cl_program program = ((module_state*)state_ptr)->program;
    halide_assert(user_context, program);
    cl_kernel f = __get_kernel(user_context, program, entry_name);
    #ifdef DEBUG
    halide_printf(user_context,
        "dev_run %s with (%dx%dx%d) blks, (%dx%dx%d) threads, %d shmem\n",
        entry_name, blocksX, blocksY, blocksZ, threadsX, threadsY, threadsZ, shared_mem_bytes
    );
    #endif
    // Pack dims
    size_t global_dim[3] = {blocksX*threadsX,  blocksY*threadsY,  blocksZ*threadsZ};
    size_t local_dim[3] = {threadsX, threadsY, threadsZ};

    // Set args
    int i = 0;
    while (arg_sizes[i] != 0) {
        #ifdef DEBUG
        halide_printf(user_context, "clSetKernelArg %i %i [0x%x ...]\n", i, arg_sizes[i], *(int *)args[i]);
        #endif
        CHECK_CALL( clSetKernelArg(f, i, arg_sizes[i], args[i]), "clSetKernelArg" );
        i++;
    }
    // Set the shared mem buffer last
    // Always set at least 1 byte of shmem, to keep the launch happy
    CHECK_CALL( clSetKernelArg(f, i, (shared_mem_bytes > 0) ? shared_mem_bytes : 1, NULL), "clSetKernelArg" );

    #ifdef DEBUG
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    // Launch kernel
    int err =
    clEnqueueNDRangeKernel(
        *cl_q,
        f,
        3,
        NULL,
        global_dim,
        local_dim,
        0, NULL, NULL
    );
    CHECK_ERR(err, "clEnqueueNDRangeKernel");

    #ifdef DEBUG
    clFinish(*cl_q);
    uint64_t t_after = halide_current_time_ns(user_context);
    halide_printf(user_context, "Kernel took: %f ms\n", (t_after - t_before) / 1000000.0);
    #endif
    return 0;
}

#ifdef TEST_STUB
const char* src = "                                               \n"\
"__kernel void knl(                                                       \n" \
"   __global float* input,                                              \n" \
"   __global float* output,                                             \n" \
"   const unsigned int count,                                           \n" \
"   __local uchar* shared)                                            \n" \
"{                                                                      \n" \
"   int i = get_global_id(0);                                           \n" \
"   if(i < count)                                                       \n" \
"       output[i] = input[i] * input[i];                                \n" \
"}                                                                      \n";

int f( buffer_t *input, buffer_t *result, int N )
{
    const char* entry_name = "knl";

    int threadsX = 128;
    int threadsY =  1;
    int threadsZ =  1;
    int blocksX = N / threadsX;
    int blocksY = 1;
    int blocksZ = 1;


    threadsX = 8;
    threadsY =  1;
    threadsZ =  1;
    blocksX = 4;
    blocksY = 4;
    blocksZ = 1;

    // Invoke
    size_t argSizes[] = { sizeof(cl_mem), sizeof(cl_mem), sizeof(int), 0 };
    void* args[] = { &input->dev, &result->dev, &N, 0 };
    halide_dev_run(
        entry_name,
        blocksX,  blocksY,  blocksZ,
        threadsX, threadsY, threadsZ,
        1, // sharedMemBytes
        argSizes,
        args
    );

    return 0;
}

int main(int argc, char* argv[]) {
    void *program = halide_init_kernels(NULL, src, 0, NULL);

    const int N = 2048;
    buffer_t in, out;

    in.dev = 0;
    in.host = (uint8_t*)malloc(N*sizeof(float));
    in.elem_size = sizeof(float);
    in.extent[0] = N; in.extent[1] = 1; in.extent[2] = 1; in.extent[3] = 1;

    out.dev = 0;
    out.host = (uint8_t*)malloc(N*sizeof(float));
    out.elem_size = sizeof(float);
    out.extent[0] = N; out.extent[1] = 1; out.extent[2] = 1; out.extent[3] = 1;

    for (int i = 0; i < N; i++) {
        ((float*)in.host)[i] = i / 2.0;
    }
    in.host_dirty = true;

    halide_dev_malloc(&in);
    halide_dev_malloc(&out);
    halide_copy_to_dev(&in);

    f( &in, &out, N );

    out.dev_dirty = true;
    halide_copy_to_host(&out);

    for (int i = 0; i < N; i++) {
        float a = ((float*)in.host)[i];
        float b = ((float*)out.host)[i];
        if (b != a*a) {
            printf("[%d] %f != %f^2\n", i, b, a);
        }
    }
}

#endif

} // extern "C" linkage

#undef CHECK_ERR
#undef CHECK_CALL
#undef TIME_START
#undef TIME_CHECK
