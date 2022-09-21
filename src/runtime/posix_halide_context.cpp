#include "HalideRuntime.h"
#include "runtime_internal.h"

extern "C" {

typedef unsigned int pthread_key_t;
typedef int pthread_once_t;
#define PTHREAD_ONCE_INIT 0

extern int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
extern int pthread_setspecific(pthread_key_t key, const void *value);
extern void *pthread_getspecific(pthread_key_t key);

extern int pthread_once(pthread_once_t *once, void (*init)());

}  // extern "C"

namespace Halide::Runtime::Internal {

// Access to `key_in_use` is controlled by this mutex
WEAK halide_mutex key_table_mutex = {{0}};
WEAK uint8_t keys_in_use[halide_context_key_count] = {0};

WEAK pthread_key_t tls_key() {
    static pthread_key_t halide_runtime_tls_key;
    static pthread_once_t halide_runtime_tls_key_once = PTHREAD_ONCE_INIT;
    (void)pthread_once(&halide_runtime_tls_key_once, []() {
        (void)pthread_key_create(&halide_runtime_tls_key, [](void *arg) { free(arg); });
    });
    return halide_runtime_tls_key;
}

WEAK halide_context_info_t *current_info() {
    pthread_key_t mk = tls_key();
    halide_context_info_t *info = (halide_context_info_t *)pthread_getspecific(mk);
    if (!info) {
        info = (halide_context_info_t *)malloc(sizeof(halide_context_info_t));
        memset(info->values, 0, sizeof(info->values));
        (void)pthread_setspecific(mk, info);
    }
    return info;
}

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

    halide_context_info_t *info = current_info();
    void *value = nullptr;
    intptr_t index = (intptr_t)key - 1;
    halide_mutex_lock(&key_table_mutex);
    if (index >= 0 && index <= halide_context_key_count && keys_in_use[index] != 0) {
        value = info->values[index];
    }
    halide_mutex_unlock(&key_table_mutex);

    return value;
}

WEAK int halide_context_set_value(halide_context_key_t key, void *value) {
    using namespace Halide::Runtime::Internal;

    halide_context_info_t *info = current_info();
    int result = halide_error_code_generic_error;
    intptr_t index = (intptr_t)key - 1;
    halide_mutex_lock(&key_table_mutex);
    if (index >= 0 && index <= halide_context_key_count && keys_in_use[index] != 0) {
        info->values[index] = value;
        result = 0;
    }
    halide_mutex_unlock(&key_table_mutex);
    return result;
}

WEAK const halide_context_info_t *halide_context_get_current_info() {
    return current_info();
}

WEAK void halide_context_set_current_info(const halide_context_info_t *info) {
    *current_info() = *info;
}

}  // extern "C"
