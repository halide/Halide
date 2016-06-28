#include "HalideRuntime.h"
#include "scoped_mutex_lock.h"

// Runtime settings for opencl and cuda device selection
namespace Halide { namespace Runtime { namespace Internal {

WEAK int gpu_device = 0;
WEAK bool gpu_device_initialized = false;
WEAK halide_mutex gpu_device_lock = { { 0 } };

}}} // namespace Halide::Runtime::Internal

extern int atoi(const char *);
extern char *getenv(const char *);

extern "C" {

WEAK void halide_set_gpu_device(int d) {
    gpu_device = d;
    gpu_device_initialized = true;
}
WEAK int halide_get_gpu_device(void *user_context) {
    ScopedMutexLock lock(&gpu_device_lock);
    if (!gpu_device_initialized) {
        const char *var = getenv("HL_GPU_DEVICE");
        if (var) {
            gpu_device = atoi(var);
        } else {
            gpu_device = -1;
        }
        gpu_device_initialized = true;
    }
    return gpu_device;
}

}
