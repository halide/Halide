#include "HalideRuntime.h"
#include "runtime_internal.h"
#include "scoped_mutex_lock.h"

extern "C" {

#ifdef BITS_64
#define WIN32API
#else
#define WIN32API __stdcall
#endif

using TlsKey = uint32_t;
using BOOL = int32_t;

extern WIN32API TlsKey TlsAlloc();
extern WIN32API BOOL TlsFree(TlsKey index);
extern WIN32API void *TlsGetValue(TlsKey index);
extern WIN32API BOOL TlsSetValue(TlsKey index, void *value);

constexpr TlsKey TLS_OUT_OF_INDEXES = 0xFFFFFFFF;

}  // extern "C"

namespace Halide::Runtime::Internal {

// Access to `key_in_use` is controlled by this mutex
WEAK halide_mutex key_table_mutex = {{0}};
WEAK uint8_t keys_in_use[halide_context_key_count] = {0};

WEAK halide_mutex tls_key_mutex = {{0}};
WEAK TlsKey tls_key() {
    // We (deliberately) build our runtime with threadsafe-static-init disabled,
    // so we must insert our own mutex guard here:

    ScopedMutexLock lock(&tls_key_mutex);
    static TlsKey halide_runtime_tls_key = []() {
        TlsKey k = TlsAlloc();
        if (k == TLS_OUT_OF_INDEXES) {
            abort();
        }
        return k;
    }();
    return halide_runtime_tls_key;
}

WEAK halide_context_info_t *current_info() {
    TlsKey k = tls_key();
    halide_context_info_t *info = (halide_context_info_t *)TlsGetValue(k);
    if (!info) {
        info = (halide_context_info_t *)malloc(sizeof(halide_context_info_t));
        memset(info->values, 0, sizeof(info->values));
        if (!TlsSetValue(k, info)) {
            abort();
        }
    }
    return info;
}

}  // namespace Halide::Runtime::Internal

extern "C" {

WEAK halide_context_key_t halide_context_allocate_key() {
    using namespace Halide::Runtime::Internal;

    ScopedMutexLock lock(&key_table_mutex);
    for (int i = 0; i < halide_context_key_count; i++) {
        if (keys_in_use[i] == 0) {
            keys_in_use[i] = 1;
            return (halide_context_key_t)(intptr_t)(i + 1);
        }
    }
    return nullptr;
}

WEAK int halide_context_free_key(halide_context_key_t key) {
    using namespace Halide::Runtime::Internal;

    ScopedMutexLock lock(&key_table_mutex);
    intptr_t index = (intptr_t)key - 1;
    if (index >= 0 && index < halide_context_key_count && keys_in_use[index] != 0) {
        keys_in_use[index] = 0;
        return 0;
    }
    return halide_error_code_generic_error;
}

WEAK void *halide_context_get_value(halide_context_key_t key) {
    using namespace Halide::Runtime::Internal;

    halide_context_info_t *info = current_info();
    intptr_t index = (intptr_t)key - 1;

    ScopedMutexLock lock(&key_table_mutex);
    if (index >= 0 && index <= halide_context_key_count && keys_in_use[index] != 0) {
        return info->values[index];
    }
    return nullptr;
}

WEAK int halide_context_set_value(halide_context_key_t key, void *value) {
    using namespace Halide::Runtime::Internal;

    halide_context_info_t *info = current_info();
    intptr_t index = (intptr_t)key - 1;
    ScopedMutexLock lock(&key_table_mutex);
    if (index >= 0 && index <= halide_context_key_count && keys_in_use[index] != 0) {
        info->values[index] = value;
        return 0;
    }
    return halide_error_code_generic_error;
}

WEAK const halide_context_info_t *halide_context_get_current_info() {
    return current_info();
}

WEAK void halide_context_set_current_info(const halide_context_info_t *info) {
    *current_info() = *info;
}

}  // extern "C"
