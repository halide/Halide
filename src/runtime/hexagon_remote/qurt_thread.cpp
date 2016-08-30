#include <HalideRuntime.h>
#include <stdlib.h>
#include <memory.h>
#include <qurt.h>

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

#define STACK_SIZE 256*1024

struct halide_thread *halide_spawn_thread(void (*f)(void *), void *closure) {
    spawned_thread *t = (spawned_thread *)malloc(sizeof(spawned_thread));
    t->f = f;
    t->closure = closure;
    t->stack = memalign(128, STACK_SIZE);
    memset(&t->handle, 0, sizeof(t->handle));
    qurt_thread_attr_t thread_attr;
    qurt_thread_attr_init(&thread_attr);
    qurt_thread_attr_set_stack_addr(&thread_attr, t->stack);
    qurt_thread_attr_set_stack_size(&thread_attr, STACK_SIZE);
    qurt_thread_attr_set_priority(&thread_attr, 255);
    qurt_thread_create(&t->handle.val, &thread_attr, &spawn_thread_helper, t);
    return (halide_thread *)t;
}

void halide_join_thread(struct halide_thread *thread_arg) {
    spawned_thread *t = (spawned_thread *)thread_arg;
    int ret = 0;
    qurt_thread_join(t->handle.val, &ret);
    free(t->stack);
    free(t);
}

void halide_mutex_init(halide_mutex *mutex) {
    qurt_mutex_init((qurt_mutex_t *)mutex);
}

void halide_mutex_lock(halide_mutex *mutex) {
    qurt_mutex_lock((qurt_mutex_t *)mutex);
}

void halide_mutex_unlock(halide_mutex *mutex) {
    qurt_mutex_unlock((qurt_mutex_t *)mutex);
}

void halide_mutex_destroy(halide_mutex *mutex) {
    qurt_mutex_destroy((qurt_mutex_t *)mutex);
    memset(mutex, 0, sizeof(halide_mutex));
}

struct halide_cond {
    uint64_t _private[8];
};

void halide_cond_init(struct halide_cond *cond) {
    qurt_cond_init((qurt_cond_t *)cond);
}

void halide_cond_destroy(struct halide_cond *cond) {
    qurt_cond_destroy((qurt_cond_t *)cond);
}

void halide_cond_broadcast(struct halide_cond *cond) {
    qurt_cond_broadcast((qurt_cond_t *)cond);
}

void halide_cond_wait(struct halide_cond *cond, struct halide_mutex *mutex) {
    qurt_cond_wait((qurt_cond_t *)cond, (qurt_mutex_t *)mutex);
}
