#include "HalideRuntime.h"
#include "runtime_internal.h"

namespace Halide::Runtime::Internal {

constexpr int MAX_TLS_KEYS = 16;
WEAK uint8_t keys_in_use[MAX_TLS_KEYS] = { 0 };
WEAK void *tls_key_values[MAX_TLS_KEYS];

}  // namespace Halide::Runtime::Internal

extern "C" {

WEAK halide_tls_key_t halide_allocate_tls_key() {
    using namespace Halide::Runtime::Internal;

    halide_tls_key_t key = nullptr;
    for (int i = 0; i < MAX_TLS_KEYS; i++) {
        if (keys_in_use[i] == 0) {
            keys_in_use[i] = 1;
            key = (halide_tls_key_t)(intptr_t)(i + 1);
            break;
        }
    }
    return key;
}

WEAK int halide_free_tls_key(halide_tls_key_t key) {
    using namespace Halide::Runtime::Internal;

    int result = halide_error_code_generic_error;
    intptr_t index = (intptr_t)key - 1;
    if (index >= 0 && index < MAX_TLS_KEYS && keys_in_use[index] != 0) {
        keys_in_use[index] = 0;
        result = 0;
    }
    return result;
}

WEAK void *halide_get_tls(halide_tls_key_t key) {
    using namespace Halide::Runtime::Internal;

    intptr_t index = (intptr_t)key - 1;
    if (index >= 0 && index <= MAX_TLS_KEYS && keys_in_use[index] != 0) {
        return tls_key_values[index];
    } else {
        return nullptr;
    }
}

WEAK int halide_set_tls(halide_tls_key_t key, void *value) {
    using namespace Halide::Runtime::Internal;

    intptr_t index = (intptr_t)key - 1;
    if (index >= 0 && index <= MAX_TLS_KEYS && keys_in_use[index] != 0) {
        tls_key_values[index] = value;
        return 0;
    } else {
        return halide_error_code_generic_error;
    }
}

WEAK halide_tls_info_t *halide_get_current_tls_info() {
    halide_error(nullptr, "halide_get_current_tls_info not implemented on this platform.");
    return nullptr;
}

WEAK int halide_set_current_tls_info(halide_tls_info_t *info) {
    halide_error(nullptr, "halide_set_current_tls_info not implemented on this platform.");
    return -1;
}

WEAK void halide_tls_info_addref(halide_tls_info_t *info) {
    halide_error(nullptr, "halide_tls_info_addref not implemented on this platform.");
}

WEAK void halide_tls_info_release(halide_tls_info_t *info) {
    halide_error(nullptr, "halide_tls_info_release not implemented on this platform.");
}

}  // extern "C"
