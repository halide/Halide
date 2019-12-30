#include "HalideRuntime.h"
#include "mini_qurt.h"

// TODO: consider getting rid of this
#define MAX_THREADS 256

using namespace Halide::Runtime::Internal::Qurt;

struct halide_thread {
    qurt_thread_t val;
};

namespace {
struct spawned_thread {
    void (*f)(void *);
    void *closure;
    void *stack;
    halide_thread handle;
};
void spawn_thread_helper(void *arg) {
    spawned_thread *t = (spawned_thread *)arg;
    t->f(t->closure);
}
}  // namespace

extern "C" {

extern void *memalign(size_t, size_t);

int halide_host_cpu_count() {
    // Assume a Snapdragon 820
    return 4;
}

#define STACK_SIZE 256 * 1024

WEAK uint16_t halide_qurt_default_thread_priority = 100;

WEAK void halide_set_default_thread_priority(int priority) {
    if (priority > 0xFF) {
        priority = 0xFF;  // Clamp to max priority
    } else if (priority <= 0) {
        return;  // Ignore settings of zero and below
    }
    halide_qurt_default_thread_priority = priority;
}

WEAK uint16_t halide_get_default_thread_priority() {
    return halide_qurt_default_thread_priority;
}

WEAK struct halide_thread *halide_spawn_thread(void (*f)(void *), void *closure) {
    uint16_t priority = halide_get_default_thread_priority();
    spawned_thread *t = (spawned_thread *)malloc(sizeof(spawned_thread));
    t->f = f;
    t->closure = closure;
    t->stack = memalign(128, STACK_SIZE);
    memset(&t->handle, 0, sizeof(t->handle));
    qurt_thread_attr_t thread_attr;
    qurt_thread_attr_init(&thread_attr);
    qurt_thread_attr_set_stack_addr(&thread_attr, t->stack);
    qurt_thread_attr_set_stack_size(&thread_attr, STACK_SIZE);
    qurt_thread_attr_set_priority(&thread_attr, priority);
    qurt_thread_create(&t->handle.val, &thread_attr, &spawn_thread_helper, t);
    return (halide_thread *)t;
}

WEAK void halide_join_thread(struct halide_thread *thread_arg) {
    spawned_thread *t = (spawned_thread *)thread_arg;
    int ret = 0;
    qurt_thread_join(t->handle.val, &ret);
    free(t->stack);
    free(t);
}

}  // extern "C"

namespace Halide {
namespace Runtime {
namespace Internal {

namespace Synchronization {

struct thread_parker {
    qurt_mutex_t mutex;
    qurt_cond_t condvar;
    bool should_park;

#if __cplusplus >= 201103L
    thread_parker(const thread_parker &) = delete;
#endif

    __attribute__((always_inline)) thread_parker()
        : should_park(false) {
        qurt_mutex_init(&mutex);
        qurt_cond_init(&condvar);
        should_park = false;
    }

    __attribute__((always_inline)) ~thread_parker() {
        qurt_cond_destroy(&condvar);
        qurt_mutex_destroy(&mutex);
    }

    __attribute__((always_inline)) void prepare_park() {
        should_park = true;
    }

    __attribute__((always_inline)) void park() {
        qurt_mutex_lock(&mutex);
        while (should_park) {
            qurt_cond_wait(&condvar, &mutex);
        }
        qurt_mutex_unlock(&mutex);
    }

    __attribute__((always_inline)) void unpark_start() {
        qurt_mutex_lock(&mutex);
    }

    __attribute__((always_inline)) void unpark() {
        should_park = false;
        qurt_cond_signal(&condvar);
    }

    __attribute__((always_inline)) void unpark_finish() {
        qurt_mutex_unlock(&mutex);
    }
};

}  // namespace Synchronization
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#include "synchronization_common.h"

#include "thread_pool_common.h"
