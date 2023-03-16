#include "HalideRuntime.h"
#include "printer.h"

extern "C" {

// Fake mutex array. We still define a pointer to halide_mutex since empty struct leads
// to compile error (empty struct has size 0 in C, size 1 in C++).
struct halide_mutex_array {
    halide_mutex *mutex;
};

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
        auto result = halide_do_task(user_context, f, x, closure);
        if (result) {
            return result;
        }
    }
    return halide_error_code_success;
}

WEAK int halide_default_do_parallel_tasks(void *user_context, int num_tasks,
                                          struct halide_parallel_task_t *tasks,
                                          void *task_parent) {
    error(user_context) << "halide_default_do_parallel_tasks not implemented on this platform.";
    return halide_error_code_unimplemented;
}

WEAK int halide_default_semaphore_init(halide_semaphore_t *s, int n) {
    halide_error(nullptr, "halide_default_semaphore_init not implemented on this platform.");
    return 0;
}

WEAK int halide_default_semaphore_release(halide_semaphore_t *s, int n) {
    halide_error(nullptr, "halide_default_semaphore_release not implemented on this platform.");
    return 0;
}

WEAK bool halide_default_semaphore_try_acquire(halide_semaphore_t *s, int n) {
    halide_error(nullptr, "halide_default_semaphore_try_acquire not implemented on this platform.");
    return false;
}

}  // extern "C"

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK halide_do_task_t custom_do_task = halide_default_do_task;
WEAK halide_do_loop_task_t custom_do_loop_task = halide_default_do_loop_task;
WEAK halide_do_par_for_t custom_do_par_for = halide_default_do_par_for;
WEAK halide_do_parallel_tasks_t custom_do_parallel_tasks = halide_default_do_parallel_tasks;
WEAK halide_semaphore_init_t custom_semaphore_init = halide_default_semaphore_init;
WEAK halide_semaphore_try_acquire_t custom_semaphore_try_acquire = halide_default_semaphore_try_acquire;
WEAK halide_semaphore_release_t custom_semaphore_release = halide_default_semaphore_release;
WEAK halide_mutex_array halide_fake_mutex_array;

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

extern "C" {

WEAK halide_thread *halide_spawn_thread(void (*f)(void *), void *closure) {
    // We can't fake spawning a thread. Emit an error.
    halide_error(nullptr, "halide_spawn_thread not implemented on this platform.");
    return nullptr;
}

WEAK void halide_join_thread(halide_thread *thread_arg) {
    halide_error(nullptr, "halide_join_thread not implemented on this platform.");
}

// Don't need to do anything with mutexes since we are in a fake thread pool.
WEAK void halide_mutex_lock(halide_mutex *mutex) {
}

WEAK void halide_mutex_unlock(halide_mutex *mutex) {
}

// Return a fake but non-null pointer here: this can be legitimately called
// from non-threaded code that uses the .atomic() schedule directive
// (e.g. correctness/multiple_scatter). Since we don't have threads, we don't
// need to mutex to do anything, but returning a null would trigger an error
// condition that would be misrepoted as out-of-memory.
WEAK halide_mutex_array *halide_mutex_array_create(int sz) {
    return &halide_fake_mutex_array;
}

WEAK void halide_mutex_array_destroy(void *user_context, void *array) {
    // Don't destroy the array! It's just halide_fake_mutex_array!
}

WEAK int halide_mutex_array_lock(halide_mutex_array *array, int entry) {
    return halide_error_code_success;
}

WEAK int halide_mutex_array_unlock(halide_mutex_array *array, int entry) {
    return halide_error_code_success;
}

WEAK void halide_shutdown_thread_pool() {
}

WEAK int halide_set_num_threads(int n) {
    if (n != 1) {
        halide_error(nullptr, "halide_set_num_threads: only supports a value of 1 on this platform.");
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

WEAK int halide_do_loop_task(void *user_context, halide_loop_task_t f,
                             int min, int size, uint8_t *closure, void *task_parent) {
    return custom_do_loop_task(user_context, f, min, size, closure, task_parent);
}

WEAK int halide_do_parallel_tasks(void *user_context, int num_tasks,
                                  struct halide_parallel_task_t *tasks,
                                  void *task_parent) {
    return custom_do_parallel_tasks(user_context, num_tasks, tasks, task_parent);
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

}  // extern "C"
