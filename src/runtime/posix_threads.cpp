#include "HalideRuntime.h"
#include "runtime_internal.h"
#include "printer.h"

extern "C" {

// On posix platforms, there's a 1-to-1 correspondence between
// halide_* threading functions and the pthread_* functions. We take
// some liberties with the types of the opaque pointer objects to
// avoid a bunch of pointer casts.

typedef long pthread_t;
extern int pthread_create(pthread_t *, const void * attr,
                          void *(*start_routine)(void *), void * arg);
extern int pthread_join(pthread_t thread, void **retval);
extern int pthread_cond_init(halide_cond *cond, const void *attr);
extern int pthread_cond_wait(halide_cond *cond, halide_mutex *mutex);
extern int pthread_cond_broadcast(halide_cond *cond);
extern int pthread_cond_signal(halide_cond *cond);
extern int pthread_cond_destroy(halide_cond *cond);
extern int pthread_mutex_init(halide_mutex *mutex, const void *attr);
extern int pthread_mutex_lock(halide_mutex *mutex);
extern int pthread_mutex_unlock(halide_mutex *mutex);
extern int pthread_mutex_destroy(halide_mutex *mutex);

} // extern "C"

namespace Halide { namespace Runtime { namespace Internal {
struct spawned_thread {
    void (*f)(void *);
    void *closure;
    pthread_t handle;
};
WEAK void *spawn_thread_helper(void *arg) {
    spawned_thread *t = (spawned_thread *)arg;
    t->f(t->closure);
    return NULL;
}
}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK struct halide_thread *halide_spawn_thread(void (*f)(void *), void *closure) {
    spawned_thread *t = (spawned_thread *)malloc(sizeof(spawned_thread));
    t->f = f;
    t->closure = closure;
    t->handle = 0;
    pthread_create(&t->handle, NULL, spawn_thread_helper, t);
    return (halide_thread *)t;
}

WEAK void halide_join_thread(struct halide_thread *thread_arg) {
    spawned_thread *t = (spawned_thread *)thread_arg;
    void *ret = NULL;
    pthread_join(t->handle, &ret);
    free(t);
}

WEAK void halide_mutex_init(halide_mutex *mutex) {
    pthread_mutex_init(mutex, NULL);
}

WEAK void halide_mutex_lock(halide_mutex *mutex) {
    pthread_mutex_lock(mutex);
}

WEAK void halide_mutex_unlock(halide_mutex *mutex) {
    pthread_mutex_unlock(mutex);
}

WEAK void halide_mutex_destroy(halide_mutex *mutex) {
    pthread_mutex_destroy(mutex);
    memset(mutex, 0, sizeof(halide_mutex));
}

WEAK void halide_cond_init(struct halide_cond *cond) {
    pthread_cond_init(cond, NULL);
}

WEAK void halide_cond_destroy(struct halide_cond *cond) {
    pthread_cond_destroy(cond);
}

WEAK void halide_cond_broadcast(struct halide_cond *cond) {
    pthread_cond_broadcast(cond);
}

WEAK void halide_cond_signal(struct halide_cond *cond) {
    pthread_cond_signal(cond);
}

WEAK void halide_cond_wait(struct halide_cond *cond, struct halide_mutex *mutex) {
    pthread_cond_wait(cond, mutex);
}




} // extern "C"
