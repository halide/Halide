
#include "mini_stdint.h"
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

extern int64_t halide_current_time_ns();
extern void free(void *);
extern void *malloc(size_t);
extern int snprintf(char *, size_t, const char *, ...);
extern char *getenv(const char *);
extern const char * strstr(const char *, const char *);


#ifndef DEBUG
#define CHECK_ERR(e,str)
#define CHECK_CALL(c,str) (c)
#define TIME_START()
#define TIME_CHECK(str)
#else // DEBUG
#define CHECK_ERR(err,str) halide_printf("Do %s\n", str); \
                         if (err != CL_SUCCESS)           \
                            halide_printf("CL: %s returned non-success: %d\n", str, err); \
                         halide_assert(err == CL_SUCCESS)
#define CHECK_CALL(c,str) {                                                 \
    halide_printf("Do %s\n", str);                                        \
    int err = (c);                                                          \
    if (err != CL_SUCCESS)                                                  \
        halide_printf("CL: %s returned non-success: %d\n", str, err); \
    halide_assert(err == CL_SUCCESS);                                              \
} halide_current_time_ns() // just *some* expression fragment after which it's legal to put a ;
#if 0
#define TIME_START() cuEventRecord(__start, 0)
#define TIME_CHECK(str) {\
    cuEventRecord(__end, 0);                                \
    cuEventSynchronize(__end);                              \
    float msec;                                             \
    cuEventElapsedTime(&msec, __start, __end);              \
    halide_printf("Do %s\n", str);                                     \
    halide_printf("   (took %fms, t=%lld)\n", msec, (long long)halide_current_time_ns()); \
} halide_current_time_ns() // just *some* expression fragment after which it's legal to put a ;
#else
#define TIME_START()
#define TIME_CHECK(str)
#endif
#endif //DEBUG

}
extern "C" {
cl_context WEAK cl_ctx = 0;
cl_command_queue WEAK cl_q = 0;

static cl_program __mod;

// Used to create buffer_ts to track internal allocations caused by our runtime
buffer_t* WEAK __make_buffer(uint8_t* host, size_t elem_size,
                                     size_t dim0, size_t dim1,
                                     size_t dim2, size_t dim3) {
    buffer_t* buf = (buffer_t*)malloc(sizeof(buffer_t));
    buf->host = host;
    buf->dev = 0;
    buf->extent[0] = dim0;
    buf->extent[1] = dim1;
    buf->extent[2] = dim2;
    buf->extent[3] = dim3;
    buf->elem_size = elem_size;
    buf->host_dirty = false;
    buf->dev_dirty = false;
    return buf;
}

WEAK void __release_buffer(buffer_t* buf) {
    free(buf);
}

WEAK buffer_t* __malloc_buffer(int32_t size) {
    return __make_buffer((uint8_t*)malloc(size), sizeof(uint8_t), size, 1, 1, 1);
}

WEAK bool halide_validate_dev_pointer(buffer_t* buf, size_t size=0) {

    size_t real_size;
    cl_int result = clGetMemObjectInfo((cl_mem)buf->dev, CL_MEM_SIZE, sizeof(size_t), &real_size, NULL);
    if (result) {
        halide_printf("Bad device pointer %p: clGetMemObjectInfo returned %d\n", (void *)buf->dev, result);
        return false;
    }
    halide_printf("validate %p: asked for %lld, actual allocated %lld\n", (void*)buf->dev, (long long)size, (long long)real_size);
    if (size) halide_assert(real_size >= size && "Validating pointer with insufficient size");
    return true;
}

WEAK void halide_dev_free(buffer_t* buf) {

    #ifdef DEBUG
    halide_printf("In dev_free of %p - dev: 0x%p\n", buf, (void*)buf->dev);
    #endif

    // TODO: Is this check covering up a bug? Halide might just be calling dev_free on all buffers, even if they are not device buffers.
    if (buf->dev != 0)
    {
        halide_assert(halide_validate_dev_pointer(buf));
        CHECK_CALL( clReleaseMemObject((cl_mem)buf->dev), "clReleaseMemObject" );
        buf->dev = 0;
    }
}




const char * get_opencl_platform() {
    char *platform = getenv("HL_OCL_PLATFORM");
    if (platform) return platform;
    else return "";
}

WEAK void halide_init_kernels(const char* src, int size) {
    int err;
    cl_device_id dev;
    // Initialize one shared context for all Halide compiled instances
    if (!cl_ctx) {
        const cl_uint maxPlatforms = 4;
        cl_platform_id platforms[maxPlatforms];
        cl_uint platformCount = 0;
        
        err = clGetPlatformIDs( maxPlatforms, platforms, &platformCount ); 
        CHECK_ERR( err, "clGetPlatformIDs" );
        
        cl_platform_id platform = NULL;
        
        const char * name = get_opencl_platform();
        if (name != NULL) {
            for (cl_uint i = 0; i < platformCount; ++i) {
                const cl_uint maxPlatformName = 256;
                char platformName[maxPlatformName];
                err = clGetPlatformInfo( platforms[i], CL_PLATFORM_NAME, maxPlatformName, platformName, NULL );
                if (err != CL_SUCCESS) continue;

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
            halide_printf("Failed to find OpenCL platform\n");
            return;
        }
        
        #ifdef DEBUG
        const cl_uint maxPlatformName = 256;
        char platformName[maxPlatformName];
        err = clGetPlatformInfo( platform, CL_PLATFORM_NAME, maxPlatformName, platformName, NULL );
        CHECK_ERR( err, "clGetPlatformInfo" );

        halide_printf("Got platform '%s', about to create context (t=%lld)\n",
                      platformName, (long long)halide_current_time_ns());
        #endif

        // Make sure we have a device
        const cl_uint maxDevices = 4;
        cl_device_id devices[maxDevices];
        cl_uint deviceCount = 0;
        err = clGetDeviceIDs( platform, CL_DEVICE_TYPE_ALL, maxDevices, devices, &deviceCount );
        CHECK_ERR( err, "clGetDeviceIDs" );
        if (deviceCount == 0) {
            halide_printf("Failed to get device\n");
            return;
        }

        dev = devices[deviceCount-1];

        #ifdef DEBUG
        const cl_uint maxDeviceName = 256;
        char deviceName[maxDeviceName];
        err = clGetDeviceInfo( dev, CL_DEVICE_NAME, maxDeviceName, deviceName, NULL );
        CHECK_ERR( err, "clGetDeviceInfo" );

        halide_printf("Got device '%s', about to create context (t=%lld)\n",
                      deviceName, (long long)halide_current_time_ns());
        #endif


        // Create context
        cl_context_properties properties[] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0 };
        cl_ctx = clCreateContext(properties, 1, &dev, NULL, NULL, &err);
        CHECK_ERR( err, "clCreateContext" );
        // cuEventCreate(&__start, 0);
        // cuEventCreate(&__end, 0);

        halide_assert(!cl_q);
        cl_q = clCreateCommandQueue(cl_ctx, dev, 0, &err);
        CHECK_ERR( err, "clCreateCommandQueue" );
    } else {
        // Maintain ref count of context.
        clRetainContext(cl_ctx);
        clRetainCommandQueue(cl_q);
    }

    // Initialize a module for just this Halide module
    if (!__mod) {
        #ifdef DEBUG
        halide_printf("Compiling kernel (%i bytes)\n", size);
        #endif

        // Create module

        cl_device_id devices[] = { dev };
        size_t lengths[] = { size };

        if (strstr(src, "/*OpenCL C*/"))
        {
            // Program is OpenCL C.
            const char * sources[] = { src };
            __mod = clCreateProgramWithSource(cl_ctx, 1, &sources[0], lengths, &err );
            CHECK_ERR( err, "clCreateProgramWithSource" );
        }
        else
        {
            // Program is SPIR binary.
            const unsigned char * binaries[] = { (unsigned char *)src };
            __mod = clCreateProgramWithBinary(cl_ctx, 1, devices, lengths, &binaries[0], NULL, &err );
            CHECK_ERR( err, "clCreateProgramWithBinary" );
        }

        err = clBuildProgram( __mod, 1, &dev, NULL, NULL, NULL );
        if (err != CL_SUCCESS) {
            size_t len;
            char buffer[2048];

            halide_printf("Error: Failed to build program executable!\n");
            clGetProgramBuildInfo(__mod, dev, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
            halide_printf("%s\n", buffer);
            halide_assert(err == CL_SUCCESS);
        }
    }
}

// Used to generate correct timings when tracing
WEAK void halide_dev_sync() {
    clFinish(cl_q);
}

WEAK void halide_release() {
    // TODO: this is for timing; bad for release-mode performance
    #ifdef DEBUG
    halide_printf("dev_sync on exit" );
    #endif
    halide_dev_sync();
    
    // Unload the module
    if (__mod) {
        CHECK_CALL( clReleaseProgram(__mod), "clReleaseProgram" );
        __mod = 0;
    }

    // Unload context (ref counted).
    CHECK_CALL( clReleaseCommandQueue(cl_q), "clReleaseCommandQueue" );
    CHECK_CALL( clReleaseContext(cl_ctx), "clReleaseContext" );
}

static cl_kernel __get_kernel(const char* entry_name) {
    cl_kernel f;
    #ifndef DEBUG
    // char msg[1];
    #else
    char msg[256];
    snprintf(msg, 256, "get_kernel %s (t=%lld)",
             entry_name, (long long)halide_current_time_ns() );
    #endif
    // Get kernel function ptr
    TIME_START();
    int err;
    f = clCreateKernel(__mod, entry_name, &err);
    CHECK_ERR(err, "clCreateKernel");
    TIME_CHECK(msg);
    return f;
}

static cl_mem __dev_malloc(size_t bytes) {
    cl_mem p;
    #ifndef DEBUG
    // char msg[1];
    #else
    char msg[256];
    snprintf(msg, 256, "dev_malloc (%lld bytes) (t=%lld)",
             (long long)bytes, (long long)halide_current_time_ns() );
    #endif
    TIME_START();
    int err;
    p = clCreateBuffer(cl_ctx, CL_MEM_READ_WRITE, bytes, NULL, &err );
    TIME_CHECK(msg);
    #ifdef DEBUG
    halide_printf("    returned: %p (err: %d)\n", (void*)p, err);
    #endif
    halide_assert(p);
    return p;
}

static inline size_t __buf_size(buffer_t* buf) {
    size_t sz = buf->elem_size;
    if (buf->extent[0]) sz *= buf->extent[0];
    if (buf->extent[1]) sz *= buf->extent[1];
    if (buf->extent[2]) sz *= buf->extent[2];
    if (buf->extent[3]) sz *= buf->extent[3];
    halide_assert(sz);
    return sz;
}

WEAK void halide_dev_malloc(buffer_t* buf) {
    #ifdef DEBUG
    halide_printf("dev_malloc of %lld bytes (%dx%dx%dx%d %d bytes) (buf->dev = %p) buffer\n",
            (long long)__buf_size(buf), buf->extent[0], buf->extent[1], buf->extent[2], buf->extent[3], buf->elem_size, (void*)buf->dev);
    #endif
    if (buf->dev) {
        halide_assert(halide_validate_dev_pointer(buf));
        return;
    }
    size_t size = __buf_size(buf);
    buf->dev = (uint64_t)__dev_malloc(size);
    halide_assert(buf->dev);
}

WEAK void halide_copy_to_dev(buffer_t* buf) {
    if (buf->host_dirty) {
        halide_assert(buf->host && buf->dev);
        size_t size = __buf_size(buf);
        #ifdef DEBUG
        char msg[256];
        snprintf(msg, 256, "copy_to_dev (%lld bytes) %p -> %p (t=%lld)",
                 (long long)size, buf->host, (void*)buf->dev, (long long)halide_current_time_ns() );
        #endif
        halide_assert(halide_validate_dev_pointer(buf));
        TIME_START();
        int err = clEnqueueWriteBuffer( cl_q, (cl_mem)((void*)buf->dev), CL_TRUE, 0, size, buf->host, 0, NULL, NULL );
        CHECK_ERR( err, msg );
        TIME_CHECK(msg);
    }
    buf->host_dirty = false;
}

WEAK void halide_copy_to_host(buffer_t* buf) {
    if (buf->dev_dirty) {
        clFinish(cl_q); // block on completion before read back
        halide_assert(buf->host && buf->dev);
        size_t size = __buf_size(buf);
        #ifndef DEBUG
        char msg[1];
        #else
        char msg[256];
        snprintf(msg, 256, "copy_to_host (%lld bytes) %p -> %p", (long long)size, (void*)buf->dev, buf->host );
        #endif
        halide_assert(halide_validate_dev_pointer(buf, size));
        TIME_START();
        halide_printf("%s\n", msg);
        int err = clEnqueueReadBuffer( cl_q, (cl_mem)((void*)buf->dev), CL_TRUE, 0, size, buf->host, 0, NULL, NULL );
        CHECK_ERR( err, msg );
        TIME_CHECK(msg);
    }
    buf->dev_dirty = false;
}
#define _COPY_TO_HOST

WEAK void halide_dev_run(
    const char* entry_name,
    int blocksX, int blocksY, int blocksZ,
    int threadsX, int threadsY, int threadsZ,
    int shared_mem_bytes,
    size_t arg_sizes[],
    void* args[])
{
    cl_kernel f = __get_kernel(entry_name);
    #ifndef DEBUG
    char msg[1];
    #else
    char msg[256];
    snprintf(
        msg, 256,
        "dev_run %s with (%dx%dx%d) blks, (%dx%dx%d) threads, %d shmem (t=%lld)",
        entry_name, blocksX, blocksY, blocksZ, threadsX, threadsY, threadsZ, shared_mem_bytes,
        (long long)halide_current_time_ns()
    );
    #endif
    // Pack dims
    size_t global_dim[3] = {blocksX*threadsX,  blocksY*threadsY,  blocksZ*threadsZ};
    size_t local_dim[3] = {threadsX, threadsY, threadsZ};

    // Set args
    int i = 0;
    while (arg_sizes[i] != 0) {
        CHECK_CALL( clSetKernelArg(f, i, arg_sizes[i], args[i]), "clSetKernelArg" );
        i++;
    }
    // Set the shared mem buffer last
    // Always set at least 1 byte of shmem, to keep the launch happy
    //CHECK_CALL( clSetKernelArg(f, i, (shared_mem_bytes > 0) ? shared_mem_bytes : 1, NULL), "clSetKernelArg" );

    // Launch kernel
    TIME_START();
    int err =
    clEnqueueNDRangeKernel(
        cl_q,
        f,
        3,
        NULL,
        global_dim,
        local_dim,
        0, NULL, NULL
    );
    CHECK_ERR(err, "clEnqueueNDRangeKernel");
    TIME_CHECK(msg);
    halide_printf("clEnqueueNDRangeKernel: %d\n", err);
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
    halide_init_kernels(src, 0);

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
