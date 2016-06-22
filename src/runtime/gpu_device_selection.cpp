#include "HalideRuntime.h"
#include "scoped_spin_lock.h"

// Runtime settings for opencl and cuda device selection
namespace Halide { namespace Runtime { namespace Internal {

WEAK int halide_gpu_device = 0;
WEAK int halide_gpu_device_lock = 0;
WEAK bool halide_gpu_device_initialized = false;

}}} // namespace Halide::Runtime::Internal

extern int atoi(const char *);
extern char *getenv(const char *);

extern "C" {

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
