#include "runtime_internal.h"

#include "HalideRuntime.h"

// The code loader for Hexagon can't do any linking of external
// symbols. So, the hexagon remote runtime does not contain any actual
// implementations of runtime functions (e.g. halide_malloc,
// halide_do_par_for, etc.). Prior to running any Halide pipelines,
// all of the function pointers must be configured.

namespace Halide { namespace Runtime { namespace Internal {

// We can't reference any external symbols on Hexagon. So, we declare
// pointers to custom runtime functions, and require the caller to set
// them.
WEAK halide_malloc_t custom_malloc = NULL;
WEAK halide_free_t custom_free = NULL;
WEAK halide_print_t custom_print = NULL;
WEAK halide_error_handler_t custom_error_handler = NULL;
WEAK halide_do_par_for_t custom_do_par_for = NULL;
WEAK halide_do_task_t custom_do_task = NULL;

}}} // namespace Halide::Runtime::Internal

extern "C" {

// Allocator
WEAK halide_malloc_t halide_set_custom_malloc(halide_malloc_t user_malloc) {
    halide_malloc_t result = custom_malloc;
    custom_malloc = user_malloc;
    return result;
}

WEAK halide_free_t halide_set_custom_free(halide_free_t user_free) {
    halide_free_t result = custom_free;
    custom_free = user_free;
    return result;
}

WEAK void *halide_malloc(void *user_context, size_t x) {
    return custom_malloc(user_context, x);
}

WEAK void halide_free(void *user_context, void *ptr) {
    custom_free(user_context, ptr);
}

// Print
WEAK halide_print_t halide_set_custom_print(halide_print_t print) {
    halide_print_t result = custom_print;
    custom_print = print;
    return result;
}

WEAK void halide_print(void *user_context, const char *msg) {
    (*custom_print)(user_context, msg);
}

// Error handler
WEAK halide_error_handler_t halide_set_error_handler(halide_error_handler_t handler) {
    halide_error_handler_t result = handler;
    custom_error_handler = handler;
    return result;
}

WEAK void halide_error(void *user_context, const char *msg) {
    (*custom_error_handler)(user_context, msg);
}

// Thread pool
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

WEAK int halide_do_par_for(void *user_context, int (*f)(void *, int, uint8_t *),
                           int min, int size, uint8_t *closure) {
  return (*custom_do_par_for)(user_context, f, min, size, closure);
}

WEAK int halide_hexagon_init_runtime(halide_malloc_t user_malloc,
                                     halide_free_t user_free,
                                     halide_print_t print,
                                     halide_error_handler_t error_handler,
                                     halide_do_par_for_t do_par_for,
                                     halide_do_task_t do_task) {
    custom_malloc = user_malloc;
    custom_free = user_free;
    custom_print = print;
    custom_error_handler = error_handler;
    custom_do_par_for = do_par_for;
    custom_do_task = do_task;
    return 0;
}

}
