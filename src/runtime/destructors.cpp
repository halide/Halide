#include "HalideRuntime.h"

#define INLINE inline __attribute__((weak)) __attribute__((always_inline)) __attribute__((used))

extern "C" {

INLINE void call_destructor(void *user_context, void (*fn)(void *user_context, void *object), void **object, bool should_call) {
    void *o = *object;
    *object = NULL;
    // Call the function
    if (o && should_call) {
        fn(user_context, o);
    }
}
}
