#include "HalideRuntime.h"

extern "C" {

extern int pthread_getthreadid_np();

WEAK int32_t halide_current_thread_id() {
    const int32_t id = pthread_getthreadid_np();
    return id ? id : 1;
}
}
