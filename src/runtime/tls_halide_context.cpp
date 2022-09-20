#include "HalideRuntime.h"
#include "runtime_internal.h"

namespace Halide::Runtime::Internal {

// Access to `key_in_use` is controlled by this mutex
WEAK halide_mutex key_table_mutex = {{0}};
WEAK uint8_t keys_in_use[halide_context_key_count] = {0};

WEAK thread_local halide_context_info_t tls_context_info;

}  // namespace Halide::Runtime::Internal

extern "C" {

WEAK halide_context_key_t halide_context_allocate_key() {
    using namespace Halide::Runtime::Internal;

    halide_context_key_t key = nullptr;
    halide_mutex_lock(&key_table_mutex);
    for (int i = 0; i < halide_context_key_count; i++) {
        if (keys_in_use[i] == 0) {
            keys_in_use[i] = 1;
            key = (halide_context_key_t)(intptr_t)(i + 1);
            break;
        }
    }
    halide_mutex_unlock(&key_table_mutex);
    return key;
}

WEAK int halide_context_free_key(halide_context_key_t key) {
    using namespace Halide::Runtime::Internal;

    int result = halide_error_code_generic_error;
    halide_mutex_lock(&key_table_mutex);
    intptr_t index = (intptr_t)key - 1;
    if (index >= 0 && index < halide_context_key_count && keys_in_use[index] != 0) {
        keys_in_use[index] = 0;
        result = 0;
    }
    halide_mutex_unlock(&key_table_mutex);
    return result;
}

WEAK void *halide_context_get_value(halide_context_key_t key) {
    using namespace Halide::Runtime::Internal;

    void *value = nullptr;
    intptr_t index = (intptr_t)key - 1;
    // TODO: it would reallllly be nice if we could avoid needing to
    // hold this mutex for the 'get' operation
    halide_mutex_lock(&key_table_mutex);
    if (index >= 0 && index <= halide_context_key_count && keys_in_use[index] != 0) {
        value = tls_context_info.values[index];
    }
    halide_mutex_unlock(&key_table_mutex);

    return value;
}

WEAK int halide_context_set_value(halide_context_key_t key, void *value) {
    using namespace Halide::Runtime::Internal;

    int result = halide_error_code_generic_error;
    intptr_t index = (intptr_t)key - 1;
    halide_mutex_lock(&key_table_mutex);
    if (index >= 0 && index <= halide_context_key_count && keys_in_use[index] != 0) {
        tls_context_info.values[index] = value;
        result = 0;
    }
    halide_mutex_unlock(&key_table_mutex);
    return result;
}

WEAK const halide_context_info_t *halide_context_get_current_info() {
    return &tls_context_info;
}

WEAK void halide_context_set_current_info(const halide_context_info_t *info) {
    tls_context_info = *info;
}

}  // extern "C"
