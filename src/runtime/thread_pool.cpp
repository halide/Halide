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
WEAK halide_do_loop_task_t custom_do_loop_task = halide_default_do_loop_task;
WEAK halide_do_par_for_t custom_do_par_for = halide_default_do_par_for;
WEAK halide_do_parallel_tasks_t custom_do_parallel_tasks = halide_default_do_parallel_tasks;
WEAK halide_semaphore_init_t custom_semaphore_init = halide_default_semaphore_init;
WEAK halide_semaphore_try_acquire_t custom_semaphore_try_acquire = halide_default_semaphore_try_acquire;
WEAK halide_semaphore_release_t custom_semaphore_release = halide_default_semaphore_release;
}}}

WEAK halide_do_task_t halide_set_custom_do_task(halide_do_task_t f) {
    halide_do_task_t result = custom_do_task;
    custom_do_task = f;
    return result;
}

WEAK halide_do_loop_task_t halide_set_custom_do_loop_task(halide_do_loop_task_t f) {
    halide_do_loop_task_t result = custom_do_loop_task;
    custom_do_loop_task = f;
    return result;
}

WEAK halide_do_par_for_t halide_set_custom_do_par_for(halide_do_par_for_t f) {
    halide_do_par_for_t result = custom_do_par_for;
    custom_do_par_for = f;
    return result;
}

WEAK void halide_set_custom_parallel_runtime(
    halide_do_par_for_t do_par_for,
    halide_do_task_t do_task,
    halide_do_loop_task_t do_loop_task,
    halide_do_parallel_tasks_t do_parallel_tasks,
    halide_semaphore_init_t semaphore_init,
    halide_semaphore_try_acquire_t semaphore_try_acquire,
    halide_semaphore_release_t semaphore_release) {

    custom_do_par_for = do_par_for;
    custom_do_task = do_task;
    custom_do_loop_task = do_loop_task;
    custom_do_parallel_tasks = do_parallel_tasks;
    custom_semaphore_init = semaphore_init;
    custom_semaphore_try_acquire = semaphore_try_acquire;
    custom_semaphore_release = semaphore_release;
}


WEAK int halide_do_task(void *user_context, halide_task_t f, int idx,
                        uint8_t *closure) {
    return custom_do_task(user_context, f, idx, closure);
}

WEAK int halide_do_par_for(void *user_context, halide_task_t f,
                           int min, int size, uint8_t *closure) {
    return custom_do_par_for(user_context, f, min, size, closure);
}

WEAK int halide_do_loop_task(void *user_context, halide_loop_task_t f,
                             int min, int size, uint8_t *closure) {
    return custom_do_loop_task(user_context, f, min, size, closure);
}

WEAK int halide_do_parallel_tasks(void *user_context, int num_tasks,
                                  struct halide_parallel_task_t *tasks) {
    return custom_do_parallel_tasks(user_context, num_tasks, tasks);
}

WEAK int halide_semaphore_init(struct halide_semaphore_t *sema, int count) {
    return custom_semaphore_init(sema, count);
}

WEAK int halide_semaphore_release(struct halide_semaphore_t *sema, int count) {
    return custom_semaphore_release(sema, count);
}

WEAK bool halide_semaphore_try_acquire(struct halide_semaphore_t *sema, int count) {
    return custom_semaphore_try_acquire(sema, count);
}


} // extern "C"
