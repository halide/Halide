#include "runtime_internal.h"

#include "HalideRuntime.h"

namespace Halide { namespace Runtime { namespace Internal {

WEAK halide_print_t custom_print = NULL;
WEAK halide_error_handler_t error_handler = NULL;
WEAK halide_malloc_t custom_malloc = NULL;
WEAK halide_free_t custom_free = NULL;
WEAK halide_get_symbol_t custom_get_symbol = NULL;
WEAK halide_load_library_t custom_load_library = NULL;
WEAK halide_get_library_symbol_t custom_get_library_symbol = NULL;
WEAK halide_do_task_t custom_do_task = NULL;
WEAK halide_do_par_for_t custom_do_par_for = NULL;

}}} // namespace Halide::Runtime::Interna

extern "C" {

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

WEAK halide_print_t halide_set_error_handler(halide_error_handler_t handler) {
    halide_print_t result = error_handler;
    error_handler = handler;
    return result;
}

WEAK void halide_error(void *user_context, const char *msg) {
    (*error_handler)(user_context, msg);
}

WEAK halide_print_t halide_set_custom_print(halide_print_t print) {
    halide_print_t result = custom_print;
    custom_print = print;
    return result;
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

WEAK halide_get_symbol_t halide_set_custom_get_symbol(halide_get_symbol_t f) {
    halide_get_symbol_t result = custom_get_symbol;
    custom_get_symbol = f;
    return result;
}

WEAK halide_load_library_t halide_set_custom_load_library(halide_load_library_t f) {
    halide_load_library_t result = custom_load_library;
    custom_load_library = f;
    return result;
}

WEAK halide_get_library_symbol_t halide_set_custom_get_library_symbol(halide_get_library_symbol_t f) {
    halide_get_library_symbol_t result = custom_get_library_symbol;
    custom_get_library_symbol = f;
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


WEAK void halide_print(void *user_context, const char *msg) {
    (*custom_print)(user_context, msg);
}

WEAK void *halide_get_symbol(const char *name) {
    return custom_get_symbol(name);
}

WEAK void *halide_load_library(const char *name) {
    return custom_load_library(name);
}

WEAK void *halide_get_library_symbol(void *lib, const char *name) {
    return custom_get_library_symbol(lib, name);
}

int halide_noos_set_runtime(halide_malloc_t user_malloc,
                            halide_free_t user_free,
                            halide_print_t print,
                            halide_error_handler_t error,
                            halide_do_par_for_t do_par_for,
                            halide_do_task_t do_task,
                            halide_get_symbol_t get_symbol,
                            halide_load_library_t load_library,
                            halide_get_library_symbol_t get_library_symbol) {
    halide_set_custom_malloc(user_malloc);
    halide_set_custom_free(user_free);
    halide_set_custom_print(print);
    halide_set_error_handler(error);
    halide_set_custom_do_par_for(do_par_for);
    halide_set_custom_do_task(do_task);
    halide_set_custom_get_symbol(get_symbol);
    halide_set_custom_load_library(load_library);
    halide_set_custom_get_library_symbol(get_library_symbol);

    return 0;
}

}
