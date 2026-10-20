#include "HalideRuntime.h"
#include "printer.h"
#include "runtime_atomics.h"
#include "scoped_mutex_lock.h"

/** \file Decompression of GPU kernel sources embedded by the compress_kernels
 * target feature. The actual decompression is platform specific, and lives in
 * zlib_decompress.cpp and windows_decompress.cpp. */

// Decompress src into exactly size bytes at dst. Defined by the platform
// specific module, which may be compiled with a different C++ ABI, so this has
// C linkage. Reports the reason for a failure through error().
extern "C" bool halide_decompress_kernel_source(void *user_context, const void *src, int src_size, void *dst, int size);

namespace Halide {
namespace Runtime {
namespace Internal {

// The decompressed sources are kept for the lifetime of the process, as each
// device context compiles them again. The decompressed bytes follow each node.
struct DecompressedKernels {
    DecompressedKernels *next;
    void **cache;
};

WEAK halide_mutex decompressed_kernels_mutex;
WEAK DecompressedKernels *decompressed_kernels = nullptr;

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::Internal;

extern "C" {

WEAK int halide_decompress_kernels(void *user_context, void **cache,
                                   const void *src, int src_size, int size) {
    void *result = nullptr;
    Synchronization::atomic_load_acquire(cache, &result);
    if (result) {
        return halide_error_code_success;
    }

    ScopedMutexLock lock(&decompressed_kernels_mutex);
    if (*cache) {
        // Another thread decompressed it while we waited for the lock.
        return halide_error_code_success;
    }

    debug(user_context) << "halide_decompress_kernels: " << src_size << " -> " << size << " bytes\n";

    DecompressedKernels *node = (DecompressedKernels *)malloc(sizeof(DecompressedKernels) + size);
    if (!node) {
        error(user_context) << "halide_decompress_kernels: out of memory.\n";
        return halide_error_code_out_of_memory;
    }
    void *dst = node + 1;
    if (!halide_decompress_kernel_source(user_context, src, src_size, dst, size)) {
        free(node);
        return halide_error_code_generic_error;
    }
    node->next = decompressed_kernels;
    node->cache = cache;
    decompressed_kernels = node;
    Synchronization::atomic_store_release(cache, &dst);
    return halide_error_code_success;
}

namespace {

WEAK __attribute__((destructor)) void halide_decompress_kernels_cleanup() {
    ScopedMutexLock lock(&decompressed_kernels_mutex);
    while (decompressed_kernels) {
        DecompressedKernels *next = decompressed_kernels->next;
        *decompressed_kernels->cache = nullptr;
        free(decompressed_kernels);
        decompressed_kernels = next;
    }
}

}  // namespace

}  // extern "C"
