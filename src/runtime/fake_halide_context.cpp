#include "HalideRuntime.h"
#include "runtime_internal.h"

namespace Halide::Runtime::Internal {

WEAK uint8_t halide_context_keys_in_use[halide_context_key_count] = { 0 };
WEAK void *halide_context_key_values[halide_context_key_count];

}  // namespace Halide::Runtime::Internal

extern "C" {

WEAK halide_context_key_t halide_context_allocate_key() {
    using namespace Halide::Runtime::Internal;

    halide_context_key_t key = nullptr;
    for (int i = 0; i < halide_context_key_count; i++) {
        if (halide_context_keys_in_use[i] == 0) {
            halide_context_keys_in_use[i] = 1;
            key = (halide_context_key_t)(intptr_t)(i + 1);
            break;
        }
    }
    return key;
}

WEAK int halide_context_free_key(halide_context_key_t key) {
    using namespace Halide::Runtime::Internal;

    int result = halide_error_code_generic_error;
    intptr_t index = (intptr_t)key - 1;
    if (index >= 0 && index < halide_context_key_count && halide_context_keys_in_use[index] != 0) {
        halide_context_keys_in_use[index] = 0;
        result = 0;
    }
    return result;
}

WEAK void *halide_context_get_value(halide_context_key_t key) {
    using namespace Halide::Runtime::Internal;

    intptr_t index = (intptr_t)key - 1;
    if (index >= 0 && index <= halide_context_key_count && halide_context_keys_in_use[index] != 0) {
        return halide_context_key_values[index];
    } else {
        return nullptr;
    }
}

WEAK int halide_context_set_value(halide_context_key_t key, void *value) {
    using namespace Halide::Runtime::Internal;

    intptr_t index = (intptr_t)key - 1;
    if (index >= 0 && index <= halide_context_key_count && halide_context_keys_in_use[index] != 0) {
        halide_context_key_values[index] = value;
        return 0;
    } else {
        return halide_error_code_generic_error;
    }
}

WEAK const halide_context_info_t *halide_context_get_current_info() {
    halide_error(nullptr, "halide_context_get_current_info not implemented on this platform.");
    return nullptr;
}

WEAK void halide_context_set_current_info(const halide_context_info_t *info) {
    halide_error(nullptr, "halide_context_set_current_info not implemented on this platform.");
}

}  // extern "C"
