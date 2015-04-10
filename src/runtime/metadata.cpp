#include "HalideRuntime.h"
#include "scoped_mutex_lock.h"

extern "C" {

struct _halide_runtime_internal_registered_filter_t {
    // This is a _halide_runtime_internal_registered_filter_t, but
    // recursive types currently break our method that copies types from
    // llvm module to llvm module
    void *next;
    const halide_filter_metadata_t* metadata;
    int (*argv_func)(void **args);
};

};

namespace Halide { namespace Runtime { namespace Internal {

struct list_head_t {
    halide_mutex mutex;
    _halide_runtime_internal_registered_filter_t *next;
};

WEAK list_head_t list_head;

} } }

extern "C" {

// This is looked up by name in Codegen_LLVM, which is easier to do
// for functions with plain C linkage.
WEAK void halide_runtime_internal_register_metadata(_halide_runtime_internal_registered_filter_t *info) {
    // Note that although the metadata pointer itself is valid, the contents pointed
    // to by it may not be initialized yet (since order of execution is not guaranteed in this case);
    // it is essential that this code not do anything with that pointer other than store
    // it for future use. (The name argument will always be valid here, however.)
    ScopedMutexLock lock(&list_head.mutex);
    info->next = list_head.next;
    list_head.next = info;
}

WEAK int halide_enumerate_registered_filters(void *user_context, void* enumerate_context, enumerate_func_t func) {
    ScopedMutexLock lock(&list_head.mutex);
    for (_halide_runtime_internal_registered_filter_t* f = list_head.next; f != NULL;
         f = (_halide_runtime_internal_registered_filter_t *)(f->next)) {
        int r = (*func)(enumerate_context, f->metadata, f->argv_func);
        if (r != 0) return r;
    }
    return 0;
}

}  // extern "C"
