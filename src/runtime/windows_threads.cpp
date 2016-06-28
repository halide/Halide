#include "HalideRuntime.h"
#include "runtime_internal.h"

extern "C" {

#ifdef BITS_64
#define WIN32API
#else
#define WIN32API __stdcall
#endif

// These sizes are large enough for 32-bit and 64-bit
typedef uint64_t ConditionVariable;
typedef uint64_t InitOnce;
typedef void * Thread;
typedef struct {
    uint64_t buf[5];
} CriticalSection;

extern WIN32API Thread CreateThread(void *, size_t, void *(*fn)(void *), void *, int32_t, int32_t *);
extern WIN32API void InitializeConditionVariable(ConditionVariable *);
extern WIN32API void WakeAllConditionVariable(ConditionVariable *);
extern WIN32API void SleepConditionVariableCS(ConditionVariable *, CriticalSection *, int);
extern WIN32API void InitializeCriticalSection(CriticalSection *);
extern WIN32API void DeleteCriticalSection(CriticalSection *);
extern WIN32API void EnterCriticalSection(CriticalSection *);
extern WIN32API void LeaveCriticalSection(CriticalSection *);
extern WIN32API int32_t WaitForSingleObject(Thread, int32_t timeout);
extern WIN32API bool InitOnceExecuteOnce(InitOnce *, bool WIN32API (*f)(InitOnce *, void *, void **), void *, void **);

} // extern "C"

namespace Halide { namespace Runtime { namespace Internal {

struct windows_mutex {
    InitOnce once;
    CriticalSection critical_section;
};
WEAK WIN32API bool init_mutex(InitOnce *, void *mutex_arg, void **) {
    windows_mutex *mutex = (windows_mutex *)mutex_arg;
    InitializeCriticalSection(&mutex->critical_section);
    return true;
}

struct spawned_thread {
    void(*f)(void *);
    void *closure;
    Thread handle;
};
WEAK void *spawn_thread_helper(void *arg) {
    spawned_thread *t = (spawned_thread *)arg;
    t->f(t->closure);
    return NULL;
}

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK halide_thread *halide_spawn_thread(void(*f)(void *), void *closure) {
    spawned_thread *t = (spawned_thread *)malloc(sizeof(spawned_thread));
    t->f = f;
    t->closure = closure;
    t->handle = CreateThread(NULL, 0, spawn_thread_helper, t, 0, NULL);
    return (halide_thread *)t;
}

WEAK void halide_join_thread(halide_thread *thread_arg) {
    spawned_thread *thread = (spawned_thread *)thread_arg;
    WaitForSingleObject(thread->handle, -1);
    free(thread);
}

WEAK void halide_mutex_destroy(halide_mutex *mutex_arg) {
    windows_mutex *mutex = (windows_mutex *)mutex_arg;
    if (mutex->once != 0) {
        DeleteCriticalSection(&mutex->critical_section);
        memset(mutex_arg, 0, sizeof(halide_mutex));
    }
}

WEAK void halide_mutex_lock(halide_mutex *mutex_arg) {
    windows_mutex *mutex = (windows_mutex *)mutex_arg;
    InitOnceExecuteOnce(&mutex->once, init_mutex, mutex, NULL);
    EnterCriticalSection(&mutex->critical_section);
}

WEAK void halide_mutex_unlock(halide_mutex *mutex_arg) {
    windows_mutex *mutex = (windows_mutex *)mutex_arg;
    LeaveCriticalSection(&mutex->critical_section);
}

WEAK void halide_cond_init(struct halide_cond *cond_arg) {
    ConditionVariable *cond = (ConditionVariable *)cond_arg;
    InitializeConditionVariable(cond);
}

WEAK void halide_cond_destroy(struct halide_cond *cond_arg) {
    // On windows we do not currently destroy condition
    // variables. We're still figuring out mysterious deadlocking
    // issues at process exit.
}

WEAK void halide_cond_broadcast(struct halide_cond *cond_arg) {
    ConditionVariable *cond = (ConditionVariable *)cond_arg;
    WakeAllConditionVariable(cond);
}

WEAK void halide_cond_wait(struct halide_cond *cond_arg, struct halide_mutex *mutex_arg) {
    ConditionVariable *cond = (ConditionVariable *)cond_arg;
    windows_mutex *mutex = (windows_mutex *)mutex_arg;
    SleepConditionVariableCS(cond, &mutex->critical_section, -1);
}

WEAK int halide_host_cpu_count() {
    // Apparently a standard windows environment variable
    char *num_cores = getenv("NUMBER_OF_PROCESSORS");
    if (num_cores) {
        return atoi(num_cores);
    } else {
        return 8;
    }
}

} // extern "C"
