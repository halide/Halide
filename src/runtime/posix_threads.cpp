#include "HalideRuntime.h"
#include "runtime_internal.h"

// TODO: consider getting rid of this
#define MAX_THREADS 256

extern "C" {

// This code cannot depend on system headers, hence we choose a data size which will
// be large enough for all systems we care about.
// 64 bytes covers this for both mutex and condvar. Using int64_t ensures alignment.
struct pthread_mutex_t {
    int64_t _private[8];
};

struct pthread_cond_t {
    int64_t _private[8];
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

// Indication on first machine is this is not any faster. (Probably not any slower either, but it's more complex so...)
// Checking it in to try it on a couple other machines, but for code review purposes it will likely get deleted.
#define TLS_CACHING_PARKER 0
#if TLS_CACHING_PARKER

struct pthread_once_t {
    uintptr_t _private[8];
};

typedef unsigned int pthread_key_t;

extern int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));
extern int pthread_key_create(pthread_key_t *key, void (*destructor)(void*));
extern int pthread_setspecific(pthread_key_t key, const void *value);
extern void *pthread_getspecific(pthread_key_t key);

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

#if TLS_CACHING_PARKER

pthread_once_t parker_cache_key_once;
pthread_key_t parker_cache_key;

struct cached_parker {
    pthread_mutex_t mutex;
    pthread_cond_t condvar;
};

void destruct_cached_parker(void *arg) {
    struct cached_parker *cached = (struct cached_parker *)arg;
    pthread_cond_destroy(&cached->condvar);
    pthread_mutex_destroy(&cached->mutex);
}

void make_parker_cache_key() {
    pthread_key_create(&parker_cache_key, destruct_cached_parker);
}

#endif

struct thread_parker {
#if TLS_CACHING_PARKER
    struct cached_parker *cached;
#else
    pthread_mutex_t mutex;
    pthread_cond_t condvar;
#endif
    bool should_park;

#if __cplusplus >= 201103L
    thread_parker(const thread_parker &) = delete;
#endif

    __attribute__((always_inline)) thread_parker() : should_park(false) {
 #if TLS_CACHING_PARKER
        pthread_once(&parker_cache_key_once, make_parker_cache_key);
        struct cached_parker *tls = (struct cached_parker *)pthread_getspecific(parker_cache_key);
        if (tls == NULL) {
            tls = (struct cached_parker *)malloc(sizeof(struct cached_parker));
            pthread_mutex_init(&tls->mutex, NULL);
            pthread_cond_init(&tls->condvar, NULL);
            pthread_setspecific(parker_cache_key, tls);
        }
        cached = tls;
 #else
        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&condvar, NULL);
 #endif
        should_park = false;
    }

    __attribute__((always_inline)) ~thread_parker() {
 #if TLS_CACHING_PARKER
 #else
        pthread_cond_destroy(&condvar);
        pthread_mutex_destroy(&mutex);
#endif
    }

    __attribute__((always_inline)) void prepare_park() {
        should_park = true;
    }

    __attribute__((always_inline)) void park() {
#if TLS_CACHING_PARKER
        pthread_mutex_lock(&cached->mutex);
        while (should_park) {
            pthread_cond_wait(&cached->condvar, &cached->mutex);
        }
        pthread_mutex_unlock(&cached->mutex);
#else
        pthread_mutex_lock(&mutex);
        while (should_park) {
            pthread_cond_wait(&condvar, &mutex);
        }
        pthread_mutex_unlock(&mutex);
#endif
    }

    __attribute__((always_inline)) void unpark_start() {
#if TLS_CACHING_PARKER
        pthread_mutex_lock(&cached->mutex);
#else
        pthread_mutex_lock(&mutex);
#endif
    }

    __attribute__((always_inline)) void unpark() {
        should_park = false;
#if TLS_CACHING_PARKER
        pthread_cond_signal(&cached->condvar);
#else
        pthread_cond_signal(&condvar);
#endif
    }

    __attribute__((always_inline)) void unpark_finish() {
#if TLS_CACHING_PARKER
        pthread_mutex_unlock(&cached->mutex);
#else
        pthread_mutex_unlock(&mutex);
#endif
    }
};

}}}} // namespace Halide::Runtime::Internal::Synchronization

#include "synchronization_common.h"

#include "thread_pool_common.h"
