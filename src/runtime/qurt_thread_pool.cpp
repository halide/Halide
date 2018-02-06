#include "HalideRuntime.h"
#include "mini_qurt.h"

using namespace Halide::Runtime::Internal::Qurt;

#define QURT_MUTEX_INIT_FLAG   0xFACEFACEFACEFACE         // some pattern value

extern "C" {
extern void *memalign(size_t, size_t);

} // extern C
struct halide_thread {
    qurt_thread_t val;
};

int halide_host_cpu_count() {
    // Assume a Snapdragon 820
    return 4;
}

//Wraooer that envelopes and init_flag for initialization 
typedef struct {
    qurt_mutex_t mutex;
    uint64_t init_flag;
    uint64_t _dummy[5]; 
} qurt_mutex_wrapper_t;

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
}

#define STACK_SIZE 256*1024

WEAK struct halide_thread *halide_spawn_thread(void (*f)(void *), void *closure) {
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

WEAK void halide_join_thread(struct halide_thread *thread_arg) {
    spawned_thread *t = (spawned_thread *)thread_arg;
    int ret = 0;
    qurt_thread_join(t->handle.val, &ret);
    free(t->stack);
    free(t);
}

WEAK void halide_mutex_init(halide_mutex *mutex_arg) {
    qurt_mutex_wrapper_t *pmutex = (qurt_mutex_wrapper_t *)mutex_arg;
    //TODO: not thread safe unless there is std::call_once type support
    if( pmutex->init_flag != QURT_MUTEX_INIT_FLAG) {
        pmutex->init_flag = QURT_MUTEX_INIT_FLAG;
        qurt_mutex_init(&pmutex->mutex);
    }
}

WEAK void halide_mutex_lock(halide_mutex *mutex_arg) {
    qurt_mutex_wrapper_t *pmutex = (qurt_mutex_wrapper_t *)mutex_arg;
    //check here if mutex is initialized 
    halide_assert(0, pmutex->init_flag == QURT_MUTEX_INIT_FLAG); 
    qurt_mutex_lock((qurt_mutex_t *)&pmutex->mutex);
}

WEAK void halide_mutex_unlock(halide_mutex *mutex_arg) {
    qurt_mutex_wrapper_t *pmutex = (qurt_mutex_wrapper_t *)mutex_arg;
    halide_assert(0, pmutex->init_flag == QURT_MUTEX_INIT_FLAG);
    qurt_mutex_unlock((qurt_mutex_t *)&pmutex->mutex);
}

WEAK void halide_mutex_destroy(halide_mutex *mutex_arg) {
    qurt_mutex_wrapper_t *pmutex = (qurt_mutex_wrapper_t *)mutex_arg;
    halide_assert(0, pmutex->init_flag == QURT_MUTEX_INIT_FLAG);
    qurt_mutex_destroy((qurt_mutex_t *)&pmutex->mutex);
    memset(mutex_arg, 0, sizeof(halide_mutex));
}

WEAK void halide_cond_init(struct halide_cond *cond) {
    qurt_cond_init((qurt_cond_t *)cond);
}

WEAK void halide_cond_destroy(struct halide_cond *cond) {
    qurt_cond_destroy((qurt_cond_t *)cond);
}

WEAK void halide_cond_broadcast(struct halide_cond *cond) {
    qurt_cond_broadcast((qurt_cond_t *)cond);
}

WEAK void halide_cond_wait(struct halide_cond *cond, struct halide_mutex *mutex) {
    qurt_cond_wait((qurt_cond_t *)cond, (qurt_mutex_t *)mutex);
}

#include "thread_pool_common.h"

extern "C" {

// There are two locks at play: the thread pool lock and the hvx
// context lock. To ensure there's no way anything could ever
// deadlock, we never attempt to acquire one while holding the
// other. CodeGen_Hexagon makes sure this is true by calling
// halide_qurt_hvx_unlock before calling halide_do_par_for.
WEAK int halide_do_par_for(void *user_context,
                           halide_task_t task,
                           int min, int size, uint8_t *closure) {
    // Do not initialize here. Initialize in constructor  
    /*qurt_mutex_t *mutex = (qurt_mutex_t *)(&work_queue.mutex);
    if (!work_queue.initialized) {
        // The thread pool asssumes that a zero-initialized mutex can
        // be locked. Not true on hexagon, and there doesn't seem to
        // be an init_once mechanism either. In this shim binary, it's
        // safe to assume that the first call to halide_do_par_for is
        // done by the main thread, so there's no race condition on
        // initializing this mutex.
       // qurt_mutex_init(mutex);
    }*/
    return halide_default_do_par_for(user_context, task, min, size, (uint8_t *)closure);
}

WEAK int halide_do_task(void *user_context, halide_task_t f,
                        int idx, uint8_t *closure) {
    return f(user_context, idx, closure);
}

namespace {
__attribute__((destructor))
WEAK void halide_thread_pool_cleanup() {
    halide_shutdown_thread_pool();
}
}
}
