#include "HalideRuntime.h"
#include "thread_pool_common.h"

extern "C" {

namespace {
__attribute__((destructor))
WEAK void halide_thread_pool_cleanup() {
    halide_shutdown_thread_pool();
}
}

WEAK void halide_set_num_threads(int n) {
    halide_mutex_lock(&work_queue.mutex);
    work_queue.desired_num_threads = n;
    halide_mutex_unlock(&work_queue.mutex);
}

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

WEAK int halide_do_task(void *user_context, halide_task_t f, int idx,
                        uint8_t *closure) {
    return (*custom_do_task)(user_context, f, idx, closure);
}

WEAK int halide_do_par_for(void *user_context, halide_task_t f,
                           int min, int size, uint8_t *closure) {
  return (*custom_do_par_for)(user_context, f, min, size, closure);
}

} // extern "C"
