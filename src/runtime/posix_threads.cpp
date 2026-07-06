#include "HalideRuntime.h"
#include "runtime_atomics.h"
#include "runtime_internal.h"

constexpr int MAX_THREADS = 256;

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
extern int pthread_create(pthread_t *, const void *attr,
                          void *(*start_routine)(void *), void *arg);
extern int pthread_join(pthread_t thread, void **retval);
extern int pthread_cond_init(pthread_cond_t *cond, const void *attr);
extern int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
extern int pthread_cond_signal(pthread_cond_t *cond);
extern int pthread_cond_destroy(pthread_cond_t *cond);
extern int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr);
extern int pthread_mutex_lock(pthread_mutex_t *mutex);
extern int pthread_mutex_unlock(pthread_mutex_t *mutex);
extern int pthread_mutex_destroy(pthread_mutex_t *mutex);

typedef unsigned int pthread_key_t;

extern int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
extern int pthread_setspecific(pthread_key_t key, const void *value);
extern void *pthread_getspecific(pthread_key_t key);

}  // extern "C"

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK pthread_key_t halide_thread_id_key;
WEAK int32_t halide_thread_id_key_state;
WEAK int32_t halide_next_thread_id = 1;

WEAK void init_halide_thread_id_key() {
    using namespace Synchronization;

    int32_t state;
    atomic_load_acquire(&halide_thread_id_key_state, &state);
    if (state != 2) {
        int32_t expected = 0;
        int32_t creating = 1;
        if (atomic_cas_strong_sequentially_consistent(&halide_thread_id_key_state, &expected, &creating)) {
            pthread_key_create(&halide_thread_id_key, nullptr);
            int32_t ready = 2;
            atomic_store_release(&halide_thread_id_key_state, &ready);
        } else {
            do {
                atomic_load_acquire(&halide_thread_id_key_state, &state);
            } while (state != 2);
        }
    }
}

WEAK int32_t allocate_halide_thread_id() {
    return Synchronization::atomic_fetch_add_sequentially_consistent(&halide_next_thread_id, 1);
}

WEAK void set_current_halide_thread_id(int32_t id) {
    init_halide_thread_id_key();
    pthread_setspecific(halide_thread_id_key, (void *)(intptr_t)id);
}

struct spawned_thread {
    void (*f)(void *);
    void *closure;
    pthread_t handle;
    int32_t thread_id;
};
WEAK void *spawn_thread_helper(void *arg) {
    spawned_thread *t = (spawned_thread *)arg;
    set_current_halide_thread_id(t->thread_id);
    t->f(t->closure);
    return nullptr;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

extern "C" {

using namespace Halide::Runtime::Internal;

WEAK int32_t halide_current_thread_id() {
    init_halide_thread_id_key();

    intptr_t id = (intptr_t)pthread_getspecific(halide_thread_id_key);
    if (id == 0) {
        id = allocate_halide_thread_id();
        pthread_setspecific(halide_thread_id_key, (void *)id);
    }
    return (int32_t)id;
}

WEAK struct halide_thread *halide_spawn_thread(void (*f)(void *), void *closure) {
    spawned_thread *t = (spawned_thread *)malloc(sizeof(spawned_thread));
    t->f = f;
    t->closure = closure;
    t->handle = 0;
    t->thread_id = allocate_halide_thread_id();
    pthread_create(&t->handle, nullptr, spawn_thread_helper, t);
    return (halide_thread *)t;
}

WEAK void halide_join_thread(struct halide_thread *thread_arg) {
    spawned_thread *t = (spawned_thread *)thread_arg;
    void *ret = nullptr;
    pthread_join(t->handle, &ret);
    free(t);
}
}

namespace Halide {
namespace Runtime {
namespace Internal {

namespace Synchronization {

// There is code to cache the parking object in a thread local. Other
// packages do this, but it did not seem to make a difference for
// performance on Linux and Mac OS X as initializing a mutex and
// condvar is cheap.  This code can be found in commit
// 6a1ea6d2c883353f51f62fec4c2bce129649e2a7.

struct thread_parker {
    pthread_mutex_t mutex;
    pthread_cond_t condvar;
    bool should_park = false;

    thread_parker(const thread_parker &) = delete;
    thread_parker &operator=(const thread_parker &) = delete;
    thread_parker(thread_parker &&) = delete;
    thread_parker &operator=(thread_parker &&) = delete;

    ALWAYS_INLINE thread_parker() {
        pthread_mutex_init(&mutex, nullptr);
        pthread_cond_init(&condvar, nullptr);
    }

    ALWAYS_INLINE ~thread_parker() {
        pthread_cond_destroy(&condvar);
        pthread_mutex_destroy(&mutex);
    }

    ALWAYS_INLINE void prepare_park() {
        should_park = true;
    }

    ALWAYS_INLINE void park() {
        pthread_mutex_lock(&mutex);
        while (should_park) {
            pthread_cond_wait(&condvar, &mutex);
        }
        pthread_mutex_unlock(&mutex);
    }

    ALWAYS_INLINE void unpark_start() {
        pthread_mutex_lock(&mutex);
    }

    ALWAYS_INLINE void unpark() {
        should_park = false;
        pthread_cond_signal(&condvar);
    }

    ALWAYS_INLINE void unpark_finish() {
        pthread_mutex_unlock(&mutex);
    }
};

}  // namespace Synchronization
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#include "synchronization_common.h"

#include "thread_pool_common.h"
