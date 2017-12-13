#include "HalideRuntime.h"
#include "thread_pool_common.h"

extern "C" {

namespace {
__attribute__((destructor))
WEAK void halide_thread_pool_cleanup() {
    halide_shutdown_thread_pool();
}
}

namespace Halide { namespace Runtime { namespace Internal {
WEAK halide_do_task_t custom_do_task = halide_default_do_task;
WEAK halide_do_par_for_t custom_do_par_for = halide_default_do_par_for;
WEAK halide_set_num_threads_t custom_set_num_threads = halide_default_set_num_threads;
}}}

WEAK halide_do_task_t halide_set_custom_do_task(halide_do_task_t f) {
    halide_do_task_t result = custom_do_task;
    custom_do_task = f;
    return result;
}

WEAK halide_do_par_for_t halide_set_custom_do_par_for(halide_do_par_for_t f) {
    halide_do_par_for_t result = custom_do_par_for;
    custom_do_par_for = f;
    return result;
}

WEAK halide_set_num_threads_t halide_set_custom_set_num_threads(halide_set_num_threads_t f) {
    halide_set_num_threads_t result = custom_set_num_threads;
    custom_set_num_threads = f;
    return result;
}
WEAK int halide_do_task(void *user_context, halide_task_t f, int idx,
                        uint8_t *closure) {
    return (*custom_do_task)(user_context, f, idx, closure);
}

WEAK int halide_do_par_for(void *user_context, halide_task_t f,
                           int min, int size, uint8_t *closure) {
  return (*custom_do_par_for)(user_context, f, min, size, closure);
}
WEAK int halide_set_num_threads(int n) {
    return (*custom_set_num_threads)(n);
}

} // extern "C"
