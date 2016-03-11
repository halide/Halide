#include "runtime_internal.h"

#include "HalideRuntime.h"

namespace Halide { namespace Runtime { namespace Internal {

halide_print_t custom_print = NULL;
halide_error_handler_t error_handler = NULL;
halide_malloc_t custom_malloc = NULL;
halide_free_t custom_free = NULL;

}}} // namespace Halide::Runtime::Interna

extern "C" {

halide_malloc_t halide_set_custom_malloc(halide_malloc_t user_malloc) {
    halide_malloc_t result = custom_malloc;
    custom_malloc = user_malloc;
    return result;
}

halide_free_t halide_set_custom_free(halide_free_t user_free) {
    halide_free_t result = custom_free;
    custom_free = user_free;
    return result;
}

void *halide_malloc(void *user_context, size_t x) {
    return custom_malloc(user_context, x);
}

void halide_free(void *user_context, void *ptr) {
    custom_free(user_context, ptr);
}

halide_print_t halide_set_error_handler(halide_error_handler_t handler) {
    halide_print_t result = error_handler;
    error_handler = handler;
    return result;
}

void halide_error(void *user_context, const char *msg) {
    (*error_handler)(user_context, msg);
}

halide_print_t halide_set_custom_print(halide_print_t print) {
    halide_print_t result = custom_print;
    custom_print = print;
    return result;
}

void halide_print(void *user_context, const char *msg) {
    (*custom_print)(user_context, msg);
}

int halide_noos_set_runtime(halide_malloc_t user_malloc,
                            halide_free_t user_free,
                            halide_print_t print,
                            halide_error_handler_t error,
                            halide_do_par_for_t do_par_for,
                            halide_do_task_t do_task) {
    halide_set_custom_malloc(user_malloc);
    halide_set_custom_free(user_free);
    halide_set_custom_print(print);
    halide_set_error_handler(error);
    halide_set_custom_do_par_for(do_par_for);
    halide_set_custom_do_task(do_task);
    return 0;
}

}
