#include "HalideRuntime.h"
#include "runtime_internal.h"

extern "C" {

typedef void *pthread_t;

extern int pthread_threadid_np(pthread_t thread, uint64_t *thread_id);

WEAK int32_t halide_current_thread_id() {
    uint64_t id = 0;
    const int result = pthread_threadid_np(nullptr, &id);
    const uint32_t low32 = (uint32_t)id;
    return result == 0 ? (int32_t)(low32 ? low32 : 1) : 1;
}
}
