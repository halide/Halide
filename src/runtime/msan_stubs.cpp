#include "HalideRuntime.h"

extern "C" {

WEAK int halide_msan_annotate_memory_is_initialized(void *user_context, const void *ptr, size_t len) {
    return 0;
}

WEAK int halide_msan_annotate_buffer_is_initialized(void *user_context, buffer_t *b) {
    return 0;
}

}
