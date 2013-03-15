#include <stdint.h>
#ifdef _LP64
typedef uint64_t size_t;
#else
typedef uint32_t size_t;
#endif
#define WEAK __attribute__((weak))

extern "C" {

WEAK void halide_shutdown_thread_pool() {
}

WEAK void (*halide_custom_do_task)(void (*)(int, uint8_t *), int, uint8_t *);
WEAK void set_halide_custom_do_task(void (*f)(void (*)(int, uint8_t *), int, uint8_t *)) {
    halide_custom_do_task = f;
}

WEAK void (*halide_custom_do_par_for)(void (*)(int, uint8_t *), int, int, uint8_t *);
WEAK void set_halide_custom_do_par_for(void (*f)(void (*)(int, uint8_t *), int, int, uint8_t *)) {
    halide_custom_do_par_for = f;
}

WEAK void halide_do_par_for(void (*f)(int, uint8_t *), int min, int size, uint8_t *closure) {
    if (halide_custom_do_par_for) {
        (*halide_custom_do_par_for)(f, min, size, closure);
        return;
    }

    for (int x = min; x < min + size; x++) {
        if (halide_custom_do_task) {
            halide_custom_do_task(f, x, closure);
        } else {
            f(x, closure);
        }
    }
}

}