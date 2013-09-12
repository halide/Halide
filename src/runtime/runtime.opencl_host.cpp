/*
 * Build standalone test (on Mac) with:
 *
 *   clang -framework OpenCL -DTEST_STUB runtime.opencl_host.cpp
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../buffer_t.h"

// The OpenCL host extends the x86 target
#include "posix_allocator.cpp"
#include "posix_clock.cpp"
#include "posix_error_handler.cpp"
#include "write_debug_image.cpp"
#include "posix_io.cpp"
#include "posix_math.cpp"
#ifdef _WIN32
#include "fake_thread_pool.cpp"
#else
#ifdef __APPLE__
#include "gcd_thread_pool.cpp"
#else
#include "posix_thread_pool.cpp"
#endif
#endif

// OpenCL defines are included simply by embedding the standard Khronos cl.h,
// cl_platform.h headers in the project. They are licensed permissively enough.
#include "CL/cl.h"

#define WEAK __attribute__((weak))

extern "C" {

// #define NDEBUG // disable logging/asserts for performance

#ifdef NDEBUG
#define CHECK_ERR(e,str)
#define CHECK_CALL(c,str) (c)
#define TIME_START()
#define TIME_CHECK(str)
#else // DEBUG
#define CHECK_ERR(err,str) fprintf(stderr, "Do %s\n", str); \
                         if (err != CL_SUCCESS)           \
                            fprintf(stderr, "CL: %s returned non-success: %d\n", str, err); \
                         assert(err == CL_SUCCESS)
#define CHECK_CALL(c,str) {                                                 \
    fprintf(stderr, "Do %s\n", str);                                        \
    int err = (c);                                                          \
    if (err != CL_SUCCESS)                                                  \
        fprintf(stderr, "CL: %s returned non-success: %d\n", str, err);  \
    assert(err == CL_SUCCESS);                                              \
} halide_current_time_ns() // just *some* expression fragment after which it's legal to put a ;
#if 0
#define TIME_START() cuEventRecord(__start, 0)
#define TIME_CHECK(str) {\
    cuEventRecord(__end, 0);                                \
    cuEventSynchronize(__end);                              \
    float msec;                                             \
    cuEventElapsedTime(&msec, __start, __end);              \
    printf(stderr, "Do %s\n", str);                         \
    printf("   (took %fms, t=%d)\n", msec, halide_current_time_ns());  \
} halide_current_time_ns() // just *some* expression fragment after which it's legal to put a ;
#else
#define TIME_START()
#define TIME_CHECK(str)
#endif
#endif //NDEBUG



// Device, Context, Module, and Function for this entrypoint are tracked locally
// and constructed lazily on first run.
// TODO: make __f, __mod into arrays?
// static vector<CUfunction> __f;
}
extern "C" {
cl_context WEAK cl_ctx = 0;
cl_command_queue WEAK cl_q = 0;

static cl_program __mod;
// static CUevent __start, __end;

// Used to create buffer_ts to track internal allocations caused by our runtime
buffer_t* WEAK __make_buffer(uint8_t* host, size_t elem_size,
                        size_t dim0, size_t dim1,
                        size_t dim2, size_t dim3)
{
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

WEAK void __release_buffer(buffer_t* buf)
{
    free(buf);
}
WEAK buffer_t* __malloc_buffer(int32_t size)
{
    return __make_buffer((uint8_t*)malloc(size), sizeof(uint8_t), size, 1, 1, 1);
}

WEAK bool halide_validate_dev_pointer(buffer_t* buf, size_t size=0) {

    size_t real_size;
    cl_int result = clGetMemObjectInfo((cl_mem)buf->dev, CL_MEM_SIZE, sizeof(size_t), &real_size, NULL);
    if (result) {
        fprintf(stderr, "Bad device pointer %p: clGetMemObjectInfo returned %d\n", (void *)buf->dev, result);
        return false;
    }
    fprintf(stderr, "validate %p: asked for %zu, actual allocated %zu\n", (void*)buf->dev, size, real_size);
    if (size) assert(real_size >= size && "Validating pointer with insufficient size");
    return true;
}

WEAK void halide_dev_free(buffer_t* buf) {

    #ifndef NDEBUG
    fprintf(stderr, "In dev_free of %p - dev: 0x%p\n", buf, (void*)buf->dev);
    #endif

    assert(halide_validate_dev_pointer(buf));
    CHECK_CALL( clReleaseMemObject((cl_mem)buf->dev), "clReleaseMemObject" );
    buf->dev = 0;
}

WEAK void halide_init_kernels(const char* src) {
    int err;
    cl_device_id dev;
    // Initialize one shared context for all Halide compiled instances
    if (!cl_ctx) {
        // Make sure we have a device
        const cl_uint maxDevices = 4;
        cl_device_id devices[maxDevices];
        cl_uint deviceCount = 0;
        err = clGetDeviceIDs( NULL, CL_DEVICE_TYPE_ALL, maxDevices, devices, &deviceCount );
        CHECK_ERR( err, "clGetDeviceIDs" );
        if (deviceCount == 0) {
            fprintf(stderr, "Failed to get device\n");
            return;
        }

        dev = devices[deviceCount-1];

        #ifndef NDEBUG
        fprintf(stderr, "Got device %lld, about to create context (t=%lld)\n", (long long)dev, halide_current_time_ns());
        #endif


        // Create context
        cl_ctx = clCreateContext(0, 1, &dev, NULL, NULL, &err);
        CHECK_ERR( err, "clCreateContext" );
        // cuEventCreate(&__start, 0);
        // cuEventCreate(&__end, 0);

        assert(!cl_q);
        cl_q = clCreateCommandQueue(cl_ctx, dev, 0, &err);
        CHECK_ERR( err, "clCreateCommandQueue" );
    } else {
        //CHECK_CALL( cuCtxPushCurrent(cuda_ctx), "cuCtxPushCurrent" );
    }

    // Initialize a module for just this Halide module
    if (!__mod) {
        #ifndef NDEBUG
        fprintf(stderr, "-------\nCompiling kernel source:\n%s\n--------\n", src);
        #endif

        // Create module
        __mod = clCreateProgramWithSource(cl_ctx, 1, (const char**)&src, NULL, &err );
        CHECK_ERR( err, "clCreateProgramWithSource" );

        err = clBuildProgram( __mod, 0, NULL, NULL, NULL, NULL );
        if (err != CL_SUCCESS) {
            size_t len;
            char buffer[2048];

            fprintf(stderr, "Error: Failed to build program executable!\n");
            clGetProgramBuildInfo(__mod, dev, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
            fprintf(stderr, "%s\n", buffer);
            assert(err == CL_SUCCESS);
        }
    }
}

// Used to generate correct timings when tracing
WEAK void halide_dev_sync() {
    clFinish(cl_q);
}

WEAK void halide_release() {
    // TODO: this is for timing; bad for release-mode performance
    #ifndef NDEBUG
    fprintf( stderr, "dev_sync on exit" );
    #endif
    halide_dev_sync();

    // TODO: destroy context if we own it

    // Unload the module
    if (__mod) {
        CHECK_CALL( clReleaseProgram(__mod), "clReleaseProgram" );
        __mod = 0;
    }
}

static cl_kernel __get_kernel(const char* entry_name) {
    cl_kernel f;
    #ifdef NDEBUG
    // char msg[1];
    #else
    char msg[256];
    snprintf(msg, 256, "get_kernel %s (t=%lld)", entry_name, halide_current_time_ns() );
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
    #ifdef NDEBUG
    // char msg[1];
    #else
    char msg[256];
    snprintf(msg, 256, "dev_malloc (%zu bytes) (t=%lld)", bytes, halide_current_time_ns() );
    #endif
    TIME_START();
    int err;
    p = clCreateBuffer( cl_ctx, CL_MEM_READ_WRITE, bytes, NULL, &err );
    TIME_CHECK(msg);
    #ifndef NDEBUG
    fprintf(stderr, "    returned: %p (err: %d)\n", (void*)p, err);
    #endif
    assert(p);
    return p;
}

static inline size_t buf_size(buffer_t* buf) {
    size_t sz = buf->elem_size;
    if (buf->extent[0]) sz *= buf->extent[0];
    if (buf->extent[1]) sz *= buf->extent[1];
    if (buf->extent[2]) sz *= buf->extent[2];
    if (buf->extent[3]) sz *= buf->extent[3];
    assert(sz);
    return sz;
}

WEAK void halide_dev_malloc(buffer_t* buf) {
    #ifndef NDEBUG
    fprintf(stderr, "dev_malloc of %dx%dx%dx%d (%d bytes) (buf->dev = %p) buffer\n",
            buf->extent[0], buf->extent[1], buf->extent[2], buf->extent[3], buf->elem_size, (void*)buf->dev);
    #endif
    if (buf->dev) {
        assert(halide_validate_dev_pointer(buf));
        return;
    }
    size_t size = buf_size(buf);
    buf->dev = (uint64_t)__dev_malloc(size);
    assert(buf->dev);
}

WEAK void halide_copy_to_dev(buffer_t* buf) {
    if (buf->host_dirty) {
        assert(buf->host && buf->dev);
        size_t size = buf_size(buf);
        #ifdef NDEBUG
        // char msg[1];
        #else
        char msg[256];
        snprintf(msg, 256, "copy_to_dev (%zu bytes) %p -> %p (t=%lld)", size, buf->host, (void*)buf->dev, halide_current_time_ns() );
        #endif
        assert(halide_validate_dev_pointer(buf));
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
        assert(buf->host && buf->dev);
        size_t size = buf_size(buf);
        #ifdef NDEBUG
        char msg[1];
        #else
        char msg[256];
        snprintf(msg, 256, "copy_to_host (%zu bytes) %p -> %p", size, (void*)buf->dev, buf->host );
        #endif
        assert(halide_validate_dev_pointer(buf, size));
        TIME_START();
        printf("%s\n", msg);
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
    #ifdef NDEBUG
    char msg[1];
    #else
    char msg[256];
    snprintf(
        msg, 256,
        "dev_run %s with (%dx%dx%d) blks, (%dx%dx%d) threads, %d shmem (t=%lld)",
        entry_name, blocksX, blocksY, blocksZ, threadsX, threadsY, threadsZ, shared_mem_bytes,
        halide_current_time_ns()
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
    CHECK_CALL( clSetKernelArg(f, i, (shared_mem_bytes > 0) ? shared_mem_bytes : 1, NULL), "clSetKernelArg" );

    // Launch kernel
    TIME_START();
    fprintf(stderr, "%s\n", msg);
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
    fprintf(stderr, "clEnqueueNDRangeKernel: %d\n", err);
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
    halide_init_kernels(src);

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
