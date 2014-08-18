#include "runtime_internal.h"
#include "HalideRuntime.h"
#include "scoped_spin_lock.h"

// Runtime settings for opencl and cuda device selection
namespace Halide { namespace Runtime { namespace Internal {

WEAK char halide_ocl_platform_name[256];
WEAK int halide_ocl_platform_name_lock = 0;
WEAK bool halide_ocl_platform_name_initialized = false;

WEAK char halide_ocl_device_type[256];
WEAK int halide_ocl_device_type_lock = 0;
WEAK bool halide_ocl_device_type_initialized = false;

WEAK int halide_gpu_device = 0;
WEAK int halide_gpu_device_lock = 0;
WEAK bool halide_gpu_device_initialized = false;

}}} // namespace Halide::Runtime::Internal

extern "C" {

extern char *strncpy(char *dst, const char *src, size_t n);
extern int atoi(const char *);
extern char *getenv(const char *);

WEAK void halide_set_ocl_platform_name(const char *n) {
    if (n) {
        strncpy(halide_ocl_platform_name, n, 255);
    } else {
        halide_ocl_platform_name[0] = 0;
    }
    halide_ocl_platform_name_initialized = true;
}

WEAK const char *halide_get_ocl_platform_name(void *user_context) {
    ScopedSpinLock lock(&halide_ocl_platform_name_lock);
    if (!halide_ocl_platform_name_initialized) {
        const char *name = getenv("HL_OCL_PLATFORM_NAME");
        halide_set_ocl_platform_name(name);
    }
    return halide_ocl_platform_name;
}


WEAK void halide_set_ocl_device_type(const char *n) {
    if (n) {
        strncpy(halide_ocl_device_type, n, 255);
    } else {
        halide_ocl_device_type[0] = 0;
    }
    halide_ocl_device_type_initialized = true;
}

WEAK const char *halide_get_ocl_device_type(void *user_context) {
    ScopedSpinLock lock(&halide_ocl_device_type_lock);
    if (!halide_ocl_device_type_initialized) {
        const char *name = getenv("HL_OCL_DEVICE_TYPE");
        halide_set_ocl_device_type(name);
    }
    return halide_ocl_device_type;
}


WEAK void halide_set_gpu_device(int d) {
    halide_gpu_device = d;
    halide_gpu_device_initialized = true;
}
WEAK int halide_get_gpu_device(void *user_context) {
    ScopedSpinLock lock(&halide_gpu_device_lock);
    if (!halide_gpu_device_initialized) {
        const char *var = getenv("HL_GPU_DEVICE");
        if (var) {
            halide_gpu_device = atoi(var);
        } else {
            halide_gpu_device = -1;
        }
        halide_gpu_device_initialized = true;
    }
    return halide_gpu_device;
}

}
