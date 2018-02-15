#include "HalideRuntime.h"
#include "runtime_internal.h"

// TODO: consider getting rid of this
#define MAX_THREADS 256

extern "C" {

// On posix platforms, there's a 1-to-1 correspondence between
// halide_* threading functions and the pthread_* functions. We take
// some liberties with the types of the opaque pointer objects to
// avoid a bunch of pointer casts.

struct pthread_mutex_t {
    uintptr_t _private[8];
};

struct pthread_cond_t {
    uintptr_t _private[8];
};

typedef long pthread_t;
extern int pthread_create(pthread_t *, const void * attr,
                          void *(*start_routine)(void *), void * arg);
extern int pthread_join(pthread_t thread, void **retval);
extern int pthread_cond_init(pthread_cond_t *cond, const void *attr);
extern int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
extern int pthread_cond_signal(pthread_cond_t *cond);
extern int pthread_cond_destroy(pthread_cond_t *cond);
extern int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr);
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

}

namespace Halide { namespace Runtime { namespace Internal {

namespace Synchronization {

struct thread_parker {
    pthread_mutex_t mutex;
    pthread_cond_t condvar;
    bool should_park;

    thread_parker(const thread_parker &) = delete;

    __attribute__((always_inline)) thread_parker() : should_park(false) {
        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&condvar, NULL);
        should_park = false;
    }

    __attribute__((always_inline)) ~thread_parker() {
        pthread_cond_destroy(&condvar);
        pthread_mutex_destroy(&mutex);
    }

    __attribute__((always_inline)) void prepare_park() {
        should_park = true;
    }

    __attribute__((always_inline)) void park() {
        pthread_mutex_lock(&mutex);
        while (should_park) {
            pthread_cond_wait(&condvar, &mutex);
        }
        pthread_mutex_unlock(&mutex);
    }

    __attribute__((always_inline)) void unpark_start() {
        pthread_mutex_lock(&mutex);
    }

    __attribute__((always_inline)) void unpark() {
        should_park = false;
        pthread_cond_signal(&condvar);
    }

    __attribute__((always_inline)) void unpark_finish() {
        pthread_mutex_unlock(&mutex);
    }
};

}}}} // namespace Halide::Runtime::Internal::Synchronization

#include "synchronization_common.h"

#include "thread_pool_common.h"
