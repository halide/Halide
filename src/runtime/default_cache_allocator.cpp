#include "HalideRuntime.h"

//Dummy implementation
WEAK void *halide_locked_cache_malloc(void *user_context, size_t size) {
    halide_print(user_context, "halide_locked_cache_malloc.\n");
    return halide_malloc(user_context, size);
}

//Dummy implementation
WEAK void halide_locked_cache_free(void *user_context, void *ptr) {
    halide_print(user_context, "halide_locked_cache_free.\n");
    halide_free(user_context, ptr);
}

//Dummy implementation
WEAK int halide_hexagon_free_l2_pool(void *user_context) {
    halide_print(user_context, "halide_hexagon_free_l2_pool in default cache allocator\n");
    return halide_error_code_success;
}

//Dummy implementation
WEAK int halide_hexagon_allocate_l2_pool(void *user_context, size_t size) {
   // TODO not sure what is required to be done here ?
   halide_print(user_context, "halide_hexagon_allocate_l2_pool \n");
   return halide_error_code_success;
}

