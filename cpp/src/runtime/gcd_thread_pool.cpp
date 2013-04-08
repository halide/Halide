#include <dispatch/dispatch.h>
#include <stdint.h>

#define WEAK __attribute__((weak))

extern "C" {

WEAK void halide_shutdown_thread_pool() {
}

WEAK void (*halide_custom_do_task)(void (*)(int, uint8_t *), int, uint8_t *);
WEAK void halide_set_custom_do_task(void (*f)(void (*)(int, uint8_t *), int, uint8_t *)) {
    halide_custom_do_task = f;
}

WEAK void (*halide_custom_do_par_for)(void (*)(int, uint8_t *), int, int, uint8_t *);
WEAK void halide_set_custom_do_par_for(void (*f)(void (*)(int, uint8_t *), int, int, uint8_t *)) {
    halide_custom_do_par_for = f;
}

WEAK void halide_do_task(void (*f)(int, uint8_t *), int idx, uint8_t *closure) {    
    if (halide_custom_do_task) {
        (*halide_custom_do_task)(f, idx, closure);
    } else {
        f(idx, closure);
    }
}

struct halide_gcd_job {
    void (*f)(int, uint8_t *);    
    uint8_t *closure;
    int min;
};

// Take a call from grand-central-dispatch's parallel for loop, and
// make a call to Halide's do task
WEAK void halide_do_gcd_task(void *job, size_t idx) {
    halide_gcd_job *j = (halide_gcd_job *)job;
    halide_do_task(j->f, j->min + (int)idx, j->closure);
}

WEAK void halide_do_par_for(void (*f)(int, uint8_t *), int min, int size, uint8_t *closure) {
    halide_gcd_job job;
    job.f = f;
    job.closure = closure;
    job.min = min;   
    dispatch_apply_f(size, dispatch_get_global_queue(0, 0), &job, &halide_do_gcd_task);
}

}
