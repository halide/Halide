#include "HalideRuntime.h"
#include "scoped_mutex_lock.h"

extern "C" {

struct _halide_runtime_internal_registered_filter_t {
    struct _halide_runtime_internal_registered_filter_t *next;
    const char *name;
    const halide_filter_metadata_t* metadata;
    int (*argv_func)(void **args);
};

};

namespace Halide { namespace Runtime { namespace Internal {

struct list_head_t {
    halide_mutex mutex;
    _halide_runtime_internal_registered_filter_t *next;

    list_head_t() : mutex(), next(NULL) {}
};

static list_head_t* get_list_head() {
    static list_head_t head;
    return &head;
}

} } }

extern "C" {

// This is looked up by name in Codegen_LLVM, which is easier to do
// for functions with plain C linkage.
WEAK void _halide_runtime_internal_register_metadata(_halide_runtime_internal_registered_filter_t *info) {
    // Note that although the metadata pointer itself is valid, the contents pointed
    // to by it may not be initialized yet (since order of execution is not guaranteed in this case);
    // it is essential that this code not do anything with that pointer other than store
    // it for future use. (The name argument will always be valid here, however.)
    list_head_t* head = get_list_head();
    ScopedMutexLock lock(&head->mutex);
    info->next = head->next;
    head->next = info;
}

WEAK int halide_enumerate_registered_filters(void *user_context, void* enumerate_context, enumerate_func_t func) {
    list_head_t* head = get_list_head();
    ScopedMutexLock lock(&head->mutex);
    for (_halide_runtime_internal_registered_filter_t* f = head->next; f != NULL; f = f->next) {
        int r = (*func)(enumerate_context, f->name, f->metadata, f->argv_func);
        if (r != 0) return r;
    }
    return 0;
}

}  // extern "C"
