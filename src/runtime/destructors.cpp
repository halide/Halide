#include "runtime_internal.h"

#define INLINE inline __attribute__((weak)) __attribute__((always_inline)) __attribute__((used))

extern "C" {

INLINE void call_destructor(void *user_context, void (*fn)(void *user_context, void *object), void **object) {
    void *o = *object;
    *object = NULL;
    // Call the function
    if (o) {
        fn(user_context, o);
    }
}

}
