#include "HalideRuntime.h"

extern "C" {

WEAK int halide_default_do_task(void *user_context, halide_task_t f, int idx,
                                uint8_t *closure) {
    return f(user_context, idx, closure);
}

WEAK int halide_default_do_loop_task(void *user_context, halide_loop_task_t f,
                                     int min, int extent, uint8_t *closure,
                                     void *task_parent) {
  return f(user_context, min, extent, closure, task_parent);
}

WEAK int halide_default_do_par_for(void *user_context, halide_task_t f,
                                   int min, int size, uint8_t *closure) {
    for (int x = min; x < min + size; x++) {
        int result = halide_do_task(user_context, f, x, closure);
        if (result) {
            return result;
        }
    }
    return 0;
}

WEAK int halide_default_do_parallel_tasks(void *user_context, int num_tasks,
                                          struct halide_parallel_task_t *tasks,
                                          void *task_parent) {
    halide_error(NULL, "halide_default_do_parallel_tasks not implemented on this platform.");
    return -1;
}

WEAK int halide_default_semaphore_init(halide_semaphore_t *s, int n) {
    halide_error(NULL, "halide_default_semaphore_init not implemented on this platform.");
    return 0;
}

WEAK int halide_default_semaphore_release(halide_semaphore_t *s, int n) {
    halide_error(NULL, "halide_default_semaphore_release not implemented on this platform.");
    return 0;
}

WEAK bool halide_default_semaphore_try_acquire(halide_semaphore_t *s, int n) {
    halide_error(NULL, "halide_default_semaphore_try_acquire not implemented on this platform.");
    return false;
}

}  // extern "C"

namespace Halide { namespace Runtime { namespace Internal {

WEAK halide_do_task_t custom_do_task = halide_default_do_task;
WEAK halide_do_loop_task_t custom_do_loop_task = halide_default_do_loop_task;
WEAK halide_do_par_for_t custom_do_par_for = halide_default_do_par_for;
WEAK halide_do_parallel_tasks_t custom_do_parallel_tasks = halide_default_do_parallel_tasks;
WEAK halide_semaphore_init_t custom_semaphore_init = halide_default_semaphore_init;
WEAK halide_semaphore_try_acquire_t custom_semaphore_try_acquire = halide_default_semaphore_try_acquire;
WEAK halide_semaphore_release_t custom_semaphore_release = halide_default_semaphore_release;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK void halide_sleep_ms(void *user_context, int ms) {
    // nothing
}

WEAK halide_thread *halide_spawn_thread(void (*f)(void *), void *closure) {
    // We can't fake spawning a thread. Emit an error.
    halide_error(NULL, "halide_spawn_thread not implemented on this platform.");
    return NULL;
}

WEAK void halide_join_thread(halide_thread *thread_arg) {
    halide_error(NULL, "halide_join_thread not implemented on this platform.");
}

WEAK void halide_mutex_lock(halide_mutex *mutex) {
}

WEAK void halide_mutex_unlock(halide_mutex *mutex) {
}

WEAK void halide_shutdown_thread_pool() {
}

WEAK int halide_set_num_threads(int n) {
    if (n < 0) {
        halide_error(NULL, "halide_set_num_threads: must be >= 0.");
    }
    return 1;
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

}  // extern "C"
