#include "HalideRuntime.h"
#include "runtime_internal.h"

// TODO: This code currently doesn't work on OS X (Darwin) as we
// require that locking a zero-initialized mutex works.  The fix is
// probably to use a pthread_once type mechanism to call
// pthread_mutex_init, but that requires the once initializer which
// might not be zero and is platform dependent. Thus we need our own
// portable once implementation. For now, threadpool only works on
// platforms where PTHREAD_MUTEX_INITIALIZER is zero.

extern "C" {

// On posix platforms, there's a 1-to-1 correspondence between halide_*
// threading functions and the pthread_* functions.

typedef struct {
    uint32_t flags;
    void * stack_base;
    size_t stack_size;
    size_t guard_size;
    int32_t sched_policy;
    int32_t sched_priority;
} pthread_attr_t;
typedef long pthread_t;
typedef struct {
    // 48 bytes is enough for a cond on 64-bit and 32-bit systems
    uint64_t _private[6];
} pthread_cond_t;
typedef long pthread_condattr_t;
typedef struct {
    // 64 bytes is enough for a mutex on 64-bit and 32-bit systems
    uint64_t _private[8];
} pthread_mutex_t;
typedef long pthread_mutexattr_t;

extern int pthread_create(pthread_t *, pthread_attr_t const * attr,
                          void *(*start_routine)(void *), void * arg);
extern int pthread_join(pthread_t thread, void **retval);
extern int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
extern int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
extern int pthread_cond_broadcast(pthread_cond_t *cond);
extern int pthread_cond_destroy(pthread_cond_t *cond);
extern int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
extern int pthread_mutex_lock(pthread_mutex_t *mutex);
extern int pthread_mutex_unlock(pthread_mutex_t *mutex);
extern int pthread_mutex_destroy(pthread_mutex_t *mutex);

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

WEAK void halide_mutex_lock(halide_mutex *mutex_arg) {
    pthread_mutex_t *mutex = (pthread_mutex_t *)mutex_arg;
    pthread_mutex_lock(mutex);
}

WEAK void halide_mutex_unlock(halide_mutex *mutex_arg) {
    pthread_mutex_t *mutex = (pthread_mutex_t *)mutex_arg;
    pthread_mutex_unlock(mutex);
}

WEAK void halide_mutex_destroy(halide_mutex *mutex_arg) {
    pthread_mutex_t *mutex = (pthread_mutex_t *)mutex_arg;
    pthread_mutex_destroy(mutex);
    memset(mutex_arg, 0, sizeof(halide_mutex));
}

WEAK void halide_cond_init(struct halide_cond *cond_arg) {
    pthread_cond_t *cond = (pthread_cond_t *)cond_arg;
    pthread_cond_init(cond, NULL);
}

WEAK void halide_cond_destroy(struct halide_cond *cond_arg) {
    pthread_cond_t *cond = (pthread_cond_t *)cond_arg;
    pthread_cond_destroy(cond);
}

WEAK void halide_cond_broadcast(struct halide_cond *cond_arg) {
    pthread_cond_t *cond = (pthread_cond_t *)cond_arg;
    pthread_cond_broadcast(cond);
}

WEAK void halide_cond_wait(struct halide_cond *cond_arg, struct halide_mutex *mutex_arg) {
    pthread_cond_t *cond = (pthread_cond_t *)cond_arg;
    pthread_mutex_t *mutex = (pthread_mutex_t *)mutex_arg;
    pthread_cond_wait(cond, mutex);
}

} // extern "C"
