#include "runtime_internal.h"

#include "HalideRuntime.h"

typedef int (*halide_task)(void *user_context, int, uint8_t *);

extern "C" {
WEAK int halide_do_task(void *user_context, halide_task f, int idx,
                        uint8_t *closure);
}

namespace Halide { namespace Runtime { namespace Internal {

WEAK int default_do_task(void *user_context, halide_task f, int idx,
                        uint8_t *closure) {
    return f(user_context, idx, closure);
}

WEAK int default_do_par_for(void *user_context, halide_task f,
                           int min, int size, uint8_t *closure) {
    for (int x = min; x < min + size; x++) {
        int result = halide_do_task(user_context, f, x, closure);
        if (result) {
            return result;
        }
    }
    return 0;
}

WEAK int (*halide_custom_do_task)(void *user_context, halide_task, int, uint8_t *) = default_do_task;
WEAK int (*halide_custom_do_par_for)(void *, halide_task, int, int, uint8_t *) = default_do_par_for;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK void halide_mutex_destroy(halide_mutex *mutex_arg) {
}

WEAK void halide_mutex_lock(halide_mutex *mutex) {
}

WEAK void halide_mutex_unlock(halide_mutex *mutex) {
}

WEAK void halide_shutdown_thread_pool() {
}

WEAK void halide_set_num_threads(int) {
}

WEAK int (*halide_set_custom_do_task(int (*f)(void *, halide_task, int, uint8_t *)))
           (void *, halide_task, int, uint8_t *) {
    int (*result)(void *, halide_task, int, uint8_t *) = halide_custom_do_task;
    halide_custom_do_task = f;
    return result;
}


WEAK int (*halide_set_custom_do_par_for(int (*f)(void *, halide_task, int, int, uint8_t *)))
          (void *, halide_task, int, int, uint8_t *) {
    int (*result)(void *, halide_task, int, int, uint8_t *) = halide_custom_do_par_for;
    halide_custom_do_par_for = f;
    return result;
}

WEAK int halide_do_task(void *user_context, halide_task f, int idx,
                        uint8_t *closure) {
    return (*halide_custom_do_task)(user_context, f, idx, closure);
}

WEAK int halide_do_par_for(void *user_context, int (*f)(void *, int, uint8_t *),
                           int min, int size, uint8_t *closure) {
    return (*halide_custom_do_par_for)(user_context, f, min, size, closure);
}

}
