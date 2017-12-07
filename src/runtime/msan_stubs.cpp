#include "HalideRuntime.h"

extern "C" {

WEAK void halide_msan_annotate_memory_is_initialized(void *user_context, const void *ptr, uint64_t len) {}

WEAK void halide_msan_annotate_buffer_is_initialized(void *user_context, halide_buffer_t *b) {}

WEAK void halide_msan_annotate_buffer_is_initialized_as_destructor(void *user_context, void *b) {}

}
