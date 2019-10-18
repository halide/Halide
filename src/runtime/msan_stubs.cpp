#include "HalideRuntime.h"

extern "C" {

WEAK int halide_msan_annotate_memory_is_initialized(void *user_context, const void *ptr, uint64_t len) { return 0; }

WEAK int halide_msan_annotate_buffer_is_initialized(void *user_context, halide_buffer_t *b) { return 0; }

WEAK void halide_msan_annotate_buffer_is_initialized_as_destructor(void *user_context, void *b) {}

}
