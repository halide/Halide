#include "HalideRuntime.h"

extern "C" {

WEAK void halide_msan_annotate_memory_is_initialized(void *user_context, const void *ptr, size_t len) {}

WEAK void halide_msan_annotate_buffer_is_initialized(void *user_context, buffer_t *b) {}

}
