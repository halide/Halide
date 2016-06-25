#include <HalideRuntime.h>

extern "C" {
#include <qurt.h>
#include <stdlib.h>
#include <memory.h>
}

struct halide_thread {
    qurt_thread_t val;
};

int halide_host_cpu_count() {
    return 2;
}

struct spawned_thread {
    void (*f)(void *);
    void *closure;
    halide_thread handle;
};
void spawn_thread_helper(void *arg) {
    spawned_thread *t = (spawned_thread *)arg;
    t->f(t->closure);
}

struct halide_thread *halide_spawn_thread(void (*f)(void *), void *closure) {
    spawned_thread *t = (spawned_thread *)malloc(sizeof(spawned_thread));
    t->f = f;
    t->closure = closure;
    memset(&t->handle, 0, sizeof(t->handle));
    qurt_thread_create(&t->handle.val, NULL, &spawn_thread_helper, t);
    return (halide_thread *)t;
}

void halide_join_thread(struct halide_thread *thread_arg) {
    spawned_thread *t = (spawned_thread *)thread_arg;
    int ret = 0;
    qurt_thread_join(t->handle.val, &ret);
    free(t);
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


#define WEAK
#include "../thread_pool_common.h"

extern "C" {
int halide_do_par_for(void *user_context,
                      halide_task_t task,
                      int min, int size, uint8_t *closure) {
return Halide::Runtime::Internal::default_do_par_for(user_context, task, min, size, closure);
}

int halide_do_task(void *user_context, halide_task_t f, int idx,
                       uint8_t *closure) {
// TODO: lock HVX context if necessary
return Halide::Runtime::Internal::default_do_task(user_context, f, idx, closure);
}
}
