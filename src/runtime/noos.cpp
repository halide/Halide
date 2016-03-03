#include "runtime_internal.h"

#include "HalideRuntime.h"

extern "C" int halide_do_task(void *user_context, halide_task_t f, int idx,
                              uint8_t *closure);

namespace Halide { namespace Runtime { namespace Internal {

int default_do_task(void *user_context, halide_task_t f, int idx,
                    uint8_t *closure) {
    return f(user_context, idx, closure);
}

int default_do_par_for(void *user_context, halide_task_t f,
                       int min, int size, uint8_t *closure) {
    for (int x = min; x < min + size; x++) {
        int result = halide_do_task(user_context, f, x, closure);
        if (result) {
            return result;
        }
    }
    return 0;
}

halide_do_task_t custom_do_task = default_do_task;
halide_do_par_for_t custom_do_par_for = default_do_par_for;
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

halide_do_task_t halide_set_custom_do_task(halide_do_task_t f) {
    halide_do_task_t result = custom_do_task;
    custom_do_task = f;
    return result;
}

halide_do_par_for_t halide_set_custom_do_par_for(halide_do_par_for_t f) {
    halide_do_par_for_t result = custom_do_par_for;
    custom_do_par_for = f;
    return result;
}

int halide_do_task(void *user_context, halide_task_t f, int idx,
                        uint8_t *closure) {
    return (*custom_do_task)(user_context, f, idx, closure);
}

int halide_do_par_for(void *user_context, halide_task_t f,
                           int min, int size, uint8_t *closure) {
  return (*custom_do_par_for)(user_context, f, min, size, closure);
}

int halide_noos_init_runtime(halide_malloc_t user_malloc,
                             halide_free_t user_free,
                             halide_print_t print,
                             halide_error_handler_t error,
                             halide_do_par_for_t do_par_for,
                             halide_do_task_t do_task) {
    custom_malloc = user_malloc;
    custom_free = user_free;
    custom_print = print;
    error_handler = error;
    custom_do_par_for = do_par_for;
    custom_do_task = do_task;

    print(NULL, "print!");
    halide_print(NULL, "halide_print!");
    return 0;
}

}
