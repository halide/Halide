#include "HalideRuntime.h"
#include "runtime_internal.h"

// TODO: consider getting rid of this
#define MAX_THREADS 256

extern "C" {

#ifdef BITS_64
#define WIN32API
#else
#define WIN32API __stdcall
#endif

// These sizes are large enough for 32-bit and 64-bit
typedef uint64_t ConditionVariable;
typedef void *Thread;
typedef struct {
    uint64_t buf[5];
} CriticalSection;

extern WIN32API Thread CreateThread(void *, size_t, void *(*fn)(void *), void *, int32_t, int32_t *);
extern WIN32API void InitializeConditionVariable(ConditionVariable *);
extern WIN32API void WakeConditionVariable(ConditionVariable *);
extern WIN32API void SleepConditionVariableCS(ConditionVariable *, CriticalSection *, int);
extern WIN32API void InitializeCriticalSection(CriticalSection *);
extern WIN32API void DeleteCriticalSection(CriticalSection *);
extern WIN32API void EnterCriticalSection(CriticalSection *);
extern WIN32API void LeaveCriticalSection(CriticalSection *);
extern WIN32API int32_t WaitForSingleObject(Thread, int32_t timeout);

}  // extern "C"

namespace Halide {
namespace Runtime {
namespace Internal {

struct spawned_thread {
    void (*f)(void *);
    void *closure;
    Thread handle;
};
WEAK void *spawn_thread_helper(void *arg) {
    spawned_thread *t = (spawned_thread *)arg;
    t->f(t->closure);
    return NULL;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

extern "C" {

WEAK int halide_host_cpu_count() {
    // Apparently a standard windows environment variable
    char *num_cores = getenv("NUMBER_OF_PROCESSORS");
    if (num_cores) {
        return atoi(num_cores);
    } else {
        return 8;
    }
}

WEAK halide_thread *halide_spawn_thread(void (*f)(void *), void *closure) {
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

}  // extern "C"

namespace Halide {
namespace Runtime {
namespace Internal {

namespace Synchronization {

struct thread_parker {
    CriticalSection critical_section;
    ConditionVariable condvar;
    bool should_park;

#if __cplusplus >= 201103L
    thread_parker(const thread_parker &) = delete;
#endif

    __attribute__((always_inline)) thread_parker()
        : should_park(false) {
        InitializeCriticalSection(&critical_section);
        InitializeConditionVariable(&condvar);
        should_park = false;
    }

    __attribute__((always_inline)) ~thread_parker() {
        // Windows ConditionVariable objects do not need to be deleted. There is no API to do so.
        DeleteCriticalSection(&critical_section);
    }

    __attribute__((always_inline)) void prepare_park() {
        should_park = true;
    }

    __attribute__((always_inline)) void park() {
        EnterCriticalSection(&critical_section);
        while (should_park) {
            SleepConditionVariableCS(&condvar, &critical_section, -1);
        }
        LeaveCriticalSection(&critical_section);
    }

    __attribute__((always_inline)) void unpark_start() {
        EnterCriticalSection(&critical_section);
    }

    __attribute__((always_inline)) void unpark() {
        should_park = false;
        WakeConditionVariable(&condvar);
    }

    __attribute__((always_inline)) void unpark_finish() {
        LeaveCriticalSection(&critical_section);
    }
};

}  // namespace Synchronization
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#include "synchronization_common.h"

#include "thread_pool_common.h"
