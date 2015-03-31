#include "runtime_internal.h"

#define INLINE inline __attribute__((weak)) __attribute__((always_inline)) __attribute__((used))

extern "C" {

// The destructors form a doubly-linked loop with a single sentinel
// item. The sentinel item should be stored on the stack in the
// calling function. The loop removes the need to branch or search in
// insertion and removal code.
struct destructor_t {
    void (*fn)(void *user_context, void *object);
    void *object;
    destructor_t *prev, *next;
};

// These get always-inlined away like the functions in posix_math, so
// they shouldn't start with halide_
INLINE void initialize_destructor_sentinel(destructor_t *sentinel) {
    sentinel->fn = NULL;
    sentinel->object = NULL;
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
}

INLINE void register_destructor(
    destructor_t *sentinel,       // The sentinel destructor object
    destructor_t *d,              // Empty stack space to use for this destructor object
    void (*fn)(void *, void *), // The function to call
    void *object) {             // The argument

    d->fn = fn;
    d->object = object;

    // Insert just after the sentinel.
    d->next = sentinel->next;
    d->prev = sentinel;
    sentinel->next = d;
}

INLINE void call_destructor(void *user_context, destructor_t *d) {
    // Remove myself from the list
    d->next->prev = d->prev;
    d->prev->next = d->next;

    // Call the function
    d->fn(user_context, d->object);
}

WEAK __attribute__((noinline)) void halide_call_all_destructors(void *user_context, destructor_t *sentinel) {
    destructor_t *d = sentinel->next;
    while (d != sentinel) {
        d->fn(user_context, d->object);
        d = d->next;
    }
    sentinel->next = sentinel->prev = sentinel;
}

}
