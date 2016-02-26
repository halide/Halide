#include "runtime_internal.h"

#include "HalideRuntime.h"

extern "C" {

// The code loader for Hexagon can't do any linking of external
// symbols. So, the hexagon remote runtime does not contain any actual
// implementations of runtime functions (e.g. halide_malloc,
// halide_do_par_for, etc.). Prior to running any Halide pipelines,
// all of the function pointers must be configured.

WEAK int halide_hexagon_init_runtime(halide_malloc_t user_malloc,
                                     halide_free_t user_free,
                                     halide_print_t print,
                                     halide_error_handler_t error_handler,
                                     halide_do_par_for_t do_par_for,
                                     halide_do_task_t do_task) {
    halide_set_custom_malloc(user_malloc);
    halide_set_custom_free(user_free);
    halide_set_custom_print(print);
    halide_set_error_handler(error_handler);
    halide_set_custom_do_par_for(do_par_for);
    halide_set_custom_do_task(do_task);
    return 0;
}

}
