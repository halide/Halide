#include "HalideRuntime.h"
#include "runtime_internal.h"
#include "printer.h"

extern "C" {

// On posix platforms, there's a 1-to-1 correspondence between
// system_* threading functions and the pthread_* functions. We take
// some liberties with the types of the opaque pointer objects to
// avoid a bunch of pointer casts.

/** Cross-platform mutex. These are allocated statically inside the
 * runtime, hence the fixed size. */
struct system_mutex {
    uint64_t _private[8];
};

// Condition variables. Only available on some platforms (those that use the common thread pool).
struct system_cond {
    uint64_t _private[8];
};

typedef long pthread_t;
extern int pthread_create(pthread_t *, const void * attr,
                          void *(*start_routine)(void *), void * arg);
extern int pthread_join(pthread_t thread, void **retval);
extern int pthread_cond_init(system_cond *cond, const void *attr);
extern int pthread_cond_wait(system_cond *cond, system_mutex *mutex);
extern int pthread_cond_broadcast(system_cond *cond);
extern int pthread_cond_signal(system_cond *cond);
extern int pthread_cond_destroy(system_cond *cond);
extern int pthread_mutex_init(system_mutex *mutex, const void *attr);
extern int pthread_mutex_lock(system_mutex *mutex);
extern int pthread_mutex_unlock(system_mutex *mutex);
extern int pthread_mutex_destroy(system_mutex *mutex);

#if 1
extern "C" int swtch_pri(int);
#else
extern "C" int sched_yield();
#endif

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

WEAK struct system_thread *system_spawn_thread(void (*f)(void *), void *closure) {
    spawned_thread *t = (spawned_thread *)malloc(sizeof(spawned_thread));
    t->f = f;
    t->closure = closure;
    t->handle = 0;
    pthread_create(&t->handle, NULL, spawn_thread_helper, t);
    return (system_thread *)t;
}

WEAK void system_join_thread(struct system_thread *thread_arg) {
    spawned_thread *t = (spawned_thread *)thread_arg;
    void *ret = NULL;
    pthread_join(t->handle, &ret);
    free(t);
}

// Placeholder for OS/CPU dependent thread yield support.
WEAK void system_thread_yield() {
    swtch_pri(0);
}

WEAK void system_mutex_init(system_mutex *mutex) {
    pthread_mutex_init(mutex, NULL);
}

WEAK void system_mutex_lock(system_mutex *mutex) {
    pthread_mutex_lock(mutex);
}

WEAK void system_mutex_unlock(system_mutex *mutex) {
    pthread_mutex_unlock(mutex);
}

WEAK void system_mutex_destroy(system_mutex *mutex) {
    pthread_mutex_destroy(mutex);
    memset(mutex, 0, sizeof(system_mutex));
}

WEAK void system_cond_init(struct system_cond *cond) {
    pthread_cond_init(cond, NULL);
}

WEAK void system_cond_destroy(struct system_cond *cond) {
    pthread_cond_destroy(cond);
}

WEAK void system_cond_broadcast(struct system_cond *cond) {
    pthread_cond_broadcast(cond);
}

WEAK void system_cond_signal(struct system_cond *cond) {
    pthread_cond_signal(cond);
}

WEAK void system_cond_wait(struct system_cond *cond, struct system_mutex *mutex) {
    pthread_cond_wait(cond, mutex);
}

}}} // namespace Halide::Runtime::Internal
