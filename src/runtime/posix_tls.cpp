#include "HalideRuntime.h"
#include "runtime_internal.h"
#include "printer.h"

namespace Halide::Runtime::Internal {
constexpr int MAX_TLS_KEYS = 16;
}

extern "C" {

typedef unsigned int pthread_key_t;
typedef int pthread_once_t;
#define PTHREAD_ONCE_INIT 0

extern int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
extern int pthread_setspecific(pthread_key_t key, const void *value);
extern void *pthread_getspecific(pthread_key_t key);

extern int pthread_once(pthread_once_t *once, void (*init)(void));

typedef long pthread_t;
extern pthread_t pthread_self();
extern int pthread_threadid_np(pthread_t thread, uint64_t *thread_id);

extern void *malloc(size_t);
extern void free(void *);

struct halide_tls_info_t {
    int ref_count;
    void *values[MAX_TLS_KEYS];
};

}  // extern "C"

namespace Halide::Runtime::Internal {

uint64_t _gettid() {
    uint64_t id = 0xdeadbeef;
    (void) pthread_threadid_np(pthread_self(), &id);
    return id;
}

// Access to `key_in_use` is controlled by this mutex
WEAK halide_mutex key_table_mutex = {{0}};
WEAK uint8_t keys_in_use[MAX_TLS_KEYS];

WEAK pthread_key_t halide_runtime_master_key;
pthread_once_t halide_runtime_master_key_once = PTHREAD_ONCE_INIT;

void key_destructor(void *arg) {
    halide_tls_info_t *info = (halide_tls_info_t *)arg;
    if (info) {
        halide_tls_info_release(info);
    }
}

pthread_key_t master_key() {
    (void)pthread_once(&halide_runtime_master_key_once, []() {
        (void)pthread_key_create(&halide_runtime_master_key, key_destructor);
        memset(&keys_in_use, 0, sizeof(keys_in_use));
    });
    return halide_runtime_master_key;
}

}  // namespace Halide::Runtime::Internal

extern "C" {

// TODO: currently, a halide_tls_key_t is just a slightly disguised index,
// so you could re-use a 'stale' key that happened to be reallocated.
// Should we use (say) malloc to allocate a real chunk of memory, to
// make this risk smaller?
WEAK halide_tls_key_t halide_allocate_tls_key() {
    using namespace Halide::Runtime::Internal;

    // Ensure that keys_in_use is inited
    (void)master_key();

    halide_tls_key_t key = nullptr;
    halide_mutex_lock(&key_table_mutex);
    for (int i = 0; i < MAX_TLS_KEYS; i++) {
        if (keys_in_use[i] == 0) {
            keys_in_use[i] = 1;
            key = (halide_tls_key_t)(intptr_t)(i + 1);
            break;
        }
    }
    halide_mutex_unlock(&key_table_mutex);
    return key;
}

WEAK int halide_free_tls_key(halide_tls_key_t key) {
    using namespace Halide::Runtime::Internal;

    // Ensure that keys_in_use is inited
    (void)master_key();

    int result = halide_error_code_generic_error;
    halide_mutex_lock(&key_table_mutex);
    intptr_t index = (intptr_t)key - 1;
    if (index >= 0 && index < MAX_TLS_KEYS && keys_in_use[index] != 0) {
        keys_in_use[index] = 0;
        result = 0;
    }
    halide_mutex_unlock(&key_table_mutex);
    return result;
}

WEAK void *halide_get_tls(halide_tls_key_t key) {
    using namespace Halide::Runtime::Internal;

    halide_tls_info_t *info = halide_get_current_tls_info();
    // TODO: do we need to mutex this access to keys_in_use?
    // Theoretically, maybe, but it would be bad for performance.
    //
    // TODO: should we validate that the key is allocated?
    void *value = nullptr;
    intptr_t index = (intptr_t)key - 1;
    if (index >= 0 && index <= MAX_TLS_KEYS) {
        value = info->values[index];
        // debug(nullptr)<<_gettid()<<": halide_get_tls[" << index << "] -> "<<(uintptr_t)value<<"\n";
    }

    halide_tls_info_release(info);
    return value;
}

WEAK int halide_set_tls(halide_tls_key_t key, void *value) {
    using namespace Halide::Runtime::Internal;

    halide_tls_info_t *info = halide_get_current_tls_info();
    // TODO: do we need to mutex this access to keys_in_use?
    // Theoretically, maybe, but it would be bad for performance.
    //
    // TODO: should we validate that the key is allocated?
    int result = halide_error_code_generic_error;
    intptr_t index = (intptr_t)key - 1;
    if (index >= 0 && index <= MAX_TLS_KEYS) {
        debug(nullptr)<<_gettid()<<": halide_set_tls[" << index << "] -> "<<(uintptr_t)value << " @info=" << (void*)info <<"\n";
        info->values[index] = value;
        result = 0;
    }
    halide_tls_info_release(info);
    return result;
}

WEAK halide_tls_info_t *halide_get_current_tls_info() {
    pthread_key_t mk = master_key();
    halide_tls_info_t *info = (halide_tls_info_t *)pthread_getspecific(mk);
    if (!info) {
        info = (halide_tls_info_t *)malloc(sizeof(halide_tls_info_t));
        info->ref_count = 1;
        memset(info->values, 0, sizeof(info->values));
        debug(nullptr)<<_gettid()<<": allocate new info -> "<<(void*)info<<"\n";
        (void)pthread_setspecific(mk, info);
    }
    halide_tls_info_addref(info);
    return info;
}

WEAK int halide_set_current_tls_info(halide_tls_info_t *info) {
    pthread_key_t mk = master_key();
    if (info != nullptr) {
        halide_tls_info_addref(info);
    }
    halide_tls_info_t *prev_info = (halide_tls_info_t *)pthread_getspecific(mk);
    debug(nullptr)<<_gettid()<<": halide_set_current_tls_info " << (void*)prev_info << " -> " << (void*)info <<"\n";
    if (prev_info != nullptr) {
        halide_tls_info_release(prev_info);
    }
    (void)pthread_setspecific(mk, info);
    return 0;
}

WEAK void halide_tls_info_addref(halide_tls_info_t *info) {
    __sync_add_and_fetch(&info->ref_count, 1);
}

WEAK void halide_tls_info_release(halide_tls_info_t *info) {
    if (__sync_add_and_fetch(&info->ref_count, -1) == 0) {
        free((void *)info);
    }
}

}  // extern "C"
