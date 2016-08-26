#include <HalideRuntime.h>
#include <stdlib.h>
#include <memory.h>
#include <hexagon_standalone.h>

#include "log.h"

struct halide_thread {
    int id;
};

namespace {

struct spawned_thread {
    void (*f)(void *);
    void *closure;
    void *stack;
    void *stack_end;
    halide_thread handle;
};

void spawn_thread_helper(void *arg) {
    spawned_thread *t = (spawned_thread *)arg;
    t->f(t->closure);
}

}  // namespace

#define STACK_SIZE 256*1024

struct halide_thread *halide_spawn_thread(void (*f)(void *), void *closure) {
    static volatile int next_id = 0;

    spawned_thread *t = (spawned_thread *)malloc(sizeof(spawned_thread));
    t->f = f;
    t->closure = closure;
    t->handle.id = __sync_add_and_fetch(&next_id, 1);
    t->stack = memalign(128, STACK_SIZE);
    t->stack_end = (void *) ((char *) t->stack + STACK_SIZE);
    thread_create(f, t->stack_end, t->handle.id, closure);
    return (halide_thread *)t;
}

void halide_join_thread(struct halide_thread *thread_arg) {
    spawned_thread *t = (spawned_thread *)thread_arg;
    thread_join(1 << t->handle.id);
    free(t->stack);
    free(t);
}

void halide_mutex_init(halide_mutex *mutex) {
    memset(mutex, 0, sizeof(halide_mutex));
}

void halide_mutex_lock(halide_mutex *mutex) {
    lockMutex((int *)mutex);
}

void halide_mutex_unlock(halide_mutex *mutex) {
    unlockMutex((int *)mutex);
}

void halide_mutex_destroy(halide_mutex *mutex) {
    memset(mutex, 0, sizeof(halide_mutex));
}

struct halide_cond {
    uint64_t _private2[8];
};

void halide_cond_init(struct halide_cond *cond) {
    memset(cond, 0, sizeof(halide_cond));
}

void halide_cond_destroy(struct halide_cond *cond) {
    memset(cond, 0, sizeof(halide_cond));
}

void halide_cond_broadcast(struct halide_cond *cond) {
}

void halide_cond_wait(struct halide_cond *cond, struct halide_mutex *mutex) {
    halide_mutex_unlock(mutex);

    // We don't actually need to do anything here. This will be
    // inefficient because it will cause the uses of halide_cond_wait
    // to essentially become spin locks, but it should be
    // correct. Efficiency is not that important on the simulator.

    halide_mutex_lock(mutex);
}
