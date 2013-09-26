#include "HalideRuntime.h"

#include <stdint.h>

#ifndef _SIZE_T
#ifdef __APPLE__
typedef unsigned long size_t;
#else
#ifdef _LP64
typedef uint64_t size_t;
#else
typedef uint32_t size_t;
#endif
#endif
#endif

#define WEAK __attribute__((weak))
#ifndef NULL
#define NULL 0
#endif


extern "C" {

extern void *malloc(size_t);
extern void free(void *);

WEAK void *(*halide_custom_malloc)(size_t) = NULL;
WEAK void (*halide_custom_free)(void *) = NULL;
WEAK void halide_set_custom_allocator(void *(*cust_malloc)(size_t), void (*cust_free)(void *)) {
    halide_custom_malloc = cust_malloc;
    halide_custom_free = cust_free;
}

WEAK void *halide_malloc(size_t x) {
    if (halide_custom_malloc) {
        return halide_custom_malloc(x);
    } else {
        void *orig = malloc(x+32);
        if (orig == NULL) {
            // Will result in a failed assertion and a call to halide_error
            return NULL;
        }
        // Round up to next multiple of 32. Should add at least 8 bytes so we can fit the original pointer.
        void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
        ((void **)ptr)[-1] = orig;
        return ptr;
    }
}

WEAK void halide_free(void *ptr) {
    if (halide_custom_free) {
        halide_custom_free(ptr);
    } else {
        free(((void**)ptr)[-1]);
    }
}

}
