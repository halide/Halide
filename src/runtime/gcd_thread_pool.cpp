#include "mini_stdint.h"

#define WEAK __attribute__((weak))

extern "C" {

typedef struct dispatch_queue_s *dispatch_queue_t;
typedef long dispatch_queue_priority_t;

extern dispatch_queue_t dispatch_get_global_queue(
    dispatch_queue_priority_t priority, unsigned long flags);

extern void dispatch_apply_f(size_t iterations, dispatch_queue_t queue,
                             void *context, void (*work)(void *, size_t));

WEAK void halide_shutdown_thread_pool() {
}

WEAK int (*halide_custom_do_task)(void *user_context, int (*)(void *, int, uint8_t *),
                                  int, uint8_t *);

  WEAK void halide_set_custom_do_task(int (*f)(void *, int (*)(void *, int, uint8_t *),
                                               int, uint8_t *)) {
    halide_custom_do_task = f;
}

WEAK int (*halide_custom_do_par_for)(void *, int (*)(void *, int, uint8_t *), int,
                                     int, uint8_t *);

WEAK void halide_set_custom_do_par_for(int (*f)(void *, int (*)(void *, int, uint8_t *),
                                                int, int, uint8_t *)) {
    halide_custom_do_par_for = f;
}

WEAK int halide_do_task(void *user_context, int (*f)(void *, int, uint8_t *),
                        int idx, uint8_t *closure) {
    if (halide_custom_do_task) {
        return (*halide_custom_do_task)(user_context, f, idx, closure);
    } else {
        return f(user_context, idx, closure);
    }
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

WEAK int halide_do_par_for(void *user_context, int (*f)(void *, int, uint8_t *),
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

}
