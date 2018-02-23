#include "HalideRuntimeAMDGPU.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"
#include "mini_amdgpu.h"
#include "scoped_spin_lock.h"

#define INLINE inline __attribute__((always_inline))

namespace Halide { namespace Runtime { namespace Internal {namespace Amdgpu {

#define HIP_FN(ret, fn, args) WEAK ret (HIPAPI *fn)args;

#include "hip_functions.h"

WEAK void *lib_hip = NULL;

extern "C" WEAK void *halide_amdgpu_get_symbol(void *user_context, const char *name) {
    // Only try to load the library if we can't already get the symbol
    // from the library. Even if the library is NULL, the symbols may
    // already be available in the process.
    void *symbol = halide_get_library_symbol(lib_hip, name);
    if (symbol) {
        return symbol;
    }

    const char *lib_names[] = {
        "libhip_hcc.so",
    };
    for (size_t i = 0; i < sizeof(lib_names) / sizeof(lib_names[0]); i++) {
        lib_hip = halide_load_library(lib_names[i]);
        if (lib_hip) {
            debug(user_context) << "    Loaded HIP runtime library: " << lib_names[i] << "\n";
            break;
        }
    }

    return halide_get_library_symbol(lib_hip, name);
}

template <typename T>
INLINE T get_amdgpu_symbol(void *user_context, const char *name, bool optional = false) {
    T s = (T)halide_amdgpu_get_symbol(user_context, name);
    if (!optional && !s) {
        error(user_context) << "HIP API not found: " << name << "\n";
    }
    return s;
}

// Load a HIP shared object and get the HIP API function pointers from it.
WEAK void load_libhip(void *user_context) {
    debug(user_context) << "    load_libhip (user_context: " << user_context << ")\n";
    halide_assert(user_context, hipInit == NULL);

    #define HIP_FN(ret, fn, args) fn = get_amdgpu_symbol<ret (HIPAPI *)args>(user_context, #fn);

    #include "hip_functions.h"
}

extern WEAK halide_device_interface_t amdgpu_device_interface;

WEAK const char *get_error_name(hipError_t error);
WEAK hipError_t create_amdgpu_context(void *user_context, hipCtx_t *ctx);

// A cuda context defined in this module with weak linkage
hipCtx_t WEAK context = 0;
// This spinlock protexts the above context variable.
volatile int WEAK context_lock = 0;

}}}} // namespace Halide::Runtime::Internal::Amdgpu

using namespace Halide::Runtime::Internal;
using namespace Halide::Runtime::Internal::Amdgpu;

extern "C" {
WEAK int halide_amdgpu_acquire_context(void *user_context, hipCtx_t *ctx, bool create = true) {
    // TODO: Should we use a more "assertive" assert? these asserts do
    // not block execution on failure.
    halide_assert(user_context, ctx != NULL);

    // If the context has not been initialized, initialize it now.
    halide_assert(user_context, &context != NULL);

    hipCtx_t local_val;
    __atomic_load(&context, &local_val, __ATOMIC_ACQUIRE);
    if (local_val == NULL) {
        if (!create) {
            *ctx = NULL;
            return 0;
        }

        {
            ScopedSpinLock spinlock(&context_lock);

            __atomic_load(&context, &local_val, __ATOMIC_ACQUIRE);
            if (local_val == NULL) {
                hipError_t error = create_amdgpu_context(user_context, &local_val);
                if (error != hipSuccess) {
                    __sync_lock_release(&context_lock);
                    return error;
                }
            }
            __atomic_store(&context, &local_val, __ATOMIC_RELEASE);
        }  // spinlock
    }

    *ctx = local_val;
    return 0;
}

}
