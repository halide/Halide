#include "runtime_internal.h"

extern "C" {

extern void *malloc(size_t);
extern void free(void *);

}

namespace Halide { namespace Runtime { namespace Internal {

WEAK void *default_malloc(void *user_context, size_t x) {
    // We want to return an aligned address to the application.
    // In addition, we should be able to read a double beyond the
    // buffer. So we allocate more space than what was asked for.
    const size_t alignment = 128;
    void *orig = malloc(x + alignment + sizeof(double));
    if (orig == NULL) {
        // Will result in a failed assertion and a call to halide_error
        return NULL;
    }
    void *ptr = (void *)(((size_t)orig + alignment) & ~(alignment - 1));
    ((void **)ptr)[-1] = orig;
    return ptr;
}

WEAK void default_free(void *user_context, void *ptr) {
    free(((void**)ptr)[-1]);
}

WEAK void *(*custom_malloc)(void *, size_t) = default_malloc;
WEAK void (*custom_free)(void *, void *) = default_free;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK void *(*halide_set_custom_malloc(void *(*user_malloc)(void *, size_t)))(void *, size_t) {
    void *(*result)(void *, size_t) = custom_malloc;
    custom_malloc = user_malloc;
    return result;
}

WEAK void (*halide_set_custom_free(void (*user_free)(void *, void *)))(void *, void *) {
    void (*result)(void *, void *) = custom_free;
    custom_free = user_free;
    return result;
}

WEAK void *halide_malloc(void *user_context, size_t x) {
    return custom_malloc(user_context, x);
}

WEAK void halide_free(void *user_context, void *ptr) {
    custom_free(user_context, ptr);
}

}
