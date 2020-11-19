#include "HalideRuntime.h"
#include "printer.h"

extern "C" {

ALWAYS_INLINE __attribute__((used)) void call_destructor(void *user_context, void (*fn)(void *user_context, void *object), void **object, bool should_call) {
  debug(nullptr) << "call_destructor called.\n";
    void *o = *object;
    *object = nullptr;
    // Call the function
    if (o && should_call) {
        fn(user_context, o);
    }
  debug(nullptr) << "call_destructor done.\n";
}
}
