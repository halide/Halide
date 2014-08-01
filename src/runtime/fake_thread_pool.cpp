#include "runtime_internal.h"

extern "C" {

WEAK void halide_shutdown_thread_pool() {
}

WEAK void halide_set_num_threads(int) {
}

WEAK int (*halide_custom_do_task)(void *, int (*)(void *, int, uint8_t *),
                                  int, uint8_t *);

WEAK void halide_set_custom_do_task(int (*f)(void *, int (*)(void *, int, uint8_t *),
                                             int, uint8_t *)) {
    halide_custom_do_task = f;
}

WEAK int (*halide_custom_do_par_for)(void *, int (*)(void *, int, uint8_t *), int, int, uint8_t *);

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

WEAK int halide_do_par_for(void *user_context, int (*f)(void *, int, uint8_t *),
                           int min, int size, uint8_t *closure) {
    if (halide_custom_do_par_for) {
        return (*halide_custom_do_par_for)(user_context, f, min, size, closure);
    }

    for (int x = min; x < min + size; x++) {
        int result = halide_do_task(user_context, f, x, closure);
        if (result) {
            return result;
        }
    }
    return 0;
}

}
