#include "HalideRuntime.h"

extern "C" {

typedef long dispatch_once_t;
extern void dispatch_once_f(dispatch_once_t *, void *context, void (*initializer)(void *));

typedef struct dispatch_queue_s *dispatch_queue_t;
typedef long dispatch_queue_priority_t;

extern dispatch_queue_t dispatch_get_global_queue(
    dispatch_queue_priority_t priority, unsigned long flags);

extern void dispatch_apply_f(size_t iterations, dispatch_queue_t queue,
                             void *context, void (*work)(void *, size_t));
extern void dispatch_async_f(dispatch_queue_t queue, void *context, void (*work)(void *));

typedef struct dispatch_semaphore_s *dispatch_semaphore_t;
typedef uint64_t dispatch_time_t;
#define DISPATCH_TIME_FOREVER (~0ull)

extern dispatch_semaphore_t dispatch_semaphore_create(long value);
extern long dispatch_semaphore_wait(dispatch_semaphore_t dsema, dispatch_time_t timeout);
extern long dispatch_semaphore_signal(dispatch_semaphore_t dsema);
extern void dispatch_release(void *object);


WEAK int halide_do_task(void *user_context, halide_task_t f, int idx,
                        uint8_t *closure);

}

WEAK void halide_spawn_thread(void *user_context, void (*f)(void *), void *closure) {
    dispatch_async_f(dispatch_get_global_queue(0, 0), closure, f);
}

namespace Halide { namespace Runtime { namespace Internal {

struct gcd_mutex {
    dispatch_once_t once;
    dispatch_semaphore_t semaphore;
};

WEAK void init_mutex(void *mutex_arg) {
    gcd_mutex *mutex = (gcd_mutex *)mutex_arg;
    mutex->semaphore = dispatch_semaphore_create(1);
}

WEAK int default_do_task(void *user_context, int (*f)(void *, int, uint8_t *),
                         int idx, uint8_t *closure) {
    return f(user_context, idx, closure);
}

struct halide_gcd_job {
    int (*f)(void *, int, uint8_t *);
    void *user_context;
    uint8_t *closure;
    int min;
    int exit_status;
};

// Take a call from grand-central-dispatch's parallel for loop, and
// make a call to Halide's do task
WEAK void halide_do_gcd_task(void *job, size_t idx) {
    halide_gcd_job *j = (halide_gcd_job *)job;
    j->exit_status = halide_do_task(j->user_context, j->f, j->min + (int)idx,
                                    j->closure);
}

WEAK int default_do_par_for(void *user_context, halide_task_t f,
                            int min, int size, uint8_t *closure) {
    halide_gcd_job job;
    job.f = f;
    job.user_context = user_context;
    job.closure = closure;
    job.min = min;
    job.exit_status = 0;

    dispatch_apply_f(size, dispatch_get_global_queue(0, 0), &job, &halide_do_gcd_task);
    return job.exit_status;
}

WEAK halide_do_task_t custom_do_task = default_do_task;
WEAK halide_do_par_for_t custom_do_par_for = default_do_par_for;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK void halide_mutex_cleanup(halide_mutex *mutex_arg) {
    gcd_mutex *mutex = (gcd_mutex *)mutex_arg;
    if (mutex->once != 0) {
        dispatch_release(mutex->semaphore);
        memset(mutex_arg, 0, sizeof(halide_mutex));
    }
}

WEAK void halide_mutex_lock(halide_mutex *mutex_arg) {
    gcd_mutex *mutex = (gcd_mutex *)mutex_arg;
    dispatch_once_f(&mutex->once, mutex, init_mutex);
    dispatch_semaphore_wait(mutex->semaphore, DISPATCH_TIME_FOREVER);
}

WEAK void halide_mutex_unlock(halide_mutex *mutex_arg) {
    gcd_mutex *mutex = (gcd_mutex *)mutex_arg;
    dispatch_semaphore_signal(mutex->semaphore);
}

WEAK void halide_shutdown_thread_pool() {
}

WEAK void halide_set_num_threads(int) {
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

}
