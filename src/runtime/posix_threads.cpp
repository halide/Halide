#include "HalideRuntime.h"
#include "runtime_internal.h"

// TODO: consider getting rid of this
#define MAX_THREADS 256

extern "C" {

// This code cannot depend on system headers, hence we choose a data size which will
// be large enough for all systems we care about.
// 64 bytes covers this for both mutex and condvar. Using int64_t ensures alignment.
struct pthread_mutex_t {
    uint64_t _private[8];
};

struct pthread_cond_t {
    uint64_t _private[8];
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

extern int pthread_setaffinity_np(pthread_t thread, size_t cpusetsize, const uint64_t *cpuset);

typedef unsigned int pthread_key_t;

extern int pthread_key_create(pthread_key_t *key, void (*destructor)(void*));
extern int pthread_setspecific(pthread_key_t key, const void *value);
extern void *pthread_getspecific(pthread_key_t key);

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

using namespace Halide::Runtime::Internal;

WEAK struct halide_thread *halide_spawn_thread(void (*f)(void *), void *closure) {
    spawned_thread *t = (spawned_thread *)malloc(sizeof(spawned_thread));
    t->f = f;
    t->closure = closure;
    t->handle = 0;
    pthread_create(&t->handle, NULL, spawn_thread_helper, t);

    uint64_t affinity_mask[4] = {0, 0, 0, 0};
    static uint64_t id = 0;
    affinity_mask[id >> 6] |= ((uint64_t)1) << (id & 63);
    id++;
    pthread_setaffinity_np(t->handle, sizeof(affinity_mask), affinity_mask);

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

// There is code to cache the parking object in a thread local. Other
// packages do this, but it did not seem to make a difference for
// performance on Linux and Mac OS X as initializing a mutex and
// condvar is cheap.  This code can be found in commit
// 6a1ea6d2c883353f51f62fec4c2bce129649e2a7.

struct thread_parker {
    pthread_mutex_t mutex;
    pthread_cond_t condvar;
    bool should_park;

#if __cplusplus >= 201103L
    thread_parker(const thread_parker &) = delete;
#endif

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
