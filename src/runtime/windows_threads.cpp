#include "HalideRuntime.h"
#include "runtime_internal.h"

extern "C" {

#ifdef BITS_64
#define WIN32API
#else
#define WIN32API __stdcall
#endif

// These sizes are large enough for 32-bit and 64-bit
typedef union {
    void* ptr;
    uint64_t val;
} InitOnce;
typedef void * Thread;
typedef struct {
    uint64_t buf[5];
} CriticalSection;
typedef struct {
    int waiters_count;
    int release_count;
    int generation_count;
    void* event;
    CriticalSection* lock;
} ConditionVariable;

#ifndef INFINITE
#define INFINITE 0xFFFFFFFF
#endif

extern WIN32API Thread CreateThread(void *, size_t, void *(*fn)(void *), void *, int32_t, int32_t *);
extern WIN32API void InitializeCriticalSection(CriticalSection *);
extern WIN32API void DeleteCriticalSection(CriticalSection *);
extern WIN32API void EnterCriticalSection(CriticalSection *);
extern WIN32API void LeaveCriticalSection(CriticalSection *);
extern WIN32API uint32_t WaitForSingleObject(Thread, uint32_t);
extern WIN32API void* CreateEventA(void*, int, int, const char*);
extern WIN32API bool SetEvent(void*);
extern WIN32API bool ResetEvent(void*);
extern WIN32API int CloseHandle(void*);
extern WIN32API void Sleep(uint32_t);
extern WIN32API void* LoadLibraryA(const char*);
extern WIN32API void* GetProcAddress(void*, const char*);

static bool g_Initialized = false;
static void* g_kernel32 = NULL;
static bool g_NativeCondVar = false;
static bool g_NativeInitOnce = false;

typedef void (WIN32API * fnInitializeConditionVariable)(ConditionVariable *);
typedef void (WIN32API * fnWakeAllConditionVariable)(ConditionVariable *);
typedef bool (WIN32API * fnSleepConditionVariableCS)(ConditionVariable *, CriticalSection *, uint32_t);
typedef bool (WIN32API * fnInitOnceExecuteOnce)(InitOnce *, bool (WIN32API * fn)(InitOnce *, void *, void **), void *, void **);

static fnInitializeConditionVariable pInitializeConditionVariable = NULL;
static fnWakeAllConditionVariable pWakeAllConditionVariable = NULL;
static fnSleepConditionVariableCS pSleepConditionVariableCS = NULL;
static fnInitOnceExecuteOnce pInitOnceExecuteOnce = NULL;

typedef uint32_t (WIN32API * fnInterlockedCompareExchange)(void volatile*, uint32_t, uint32_t);
typedef uint64_t (WIN32API * fnInterlockedCompareExchange64)(void volatile*, uint64_t, uint64_t);
typedef void* (WIN32API * fnInterlockedCompareExchangePointer)(void volatile*, void*, void*);

static fnInterlockedCompareExchange pInterlockedCompareExchange = NULL;
static fnInterlockedCompareExchange64 pInterlockedCompareExchange64 = NULL;
static fnInterlockedCompareExchangePointer pInterlockedCompareExchangePointer = NULL;

/**
 * Strategies for Implementing POSIX Condition Variables on Win32:
 * http://www.cs.wustl.edu/~schmidt/win32-cv-1.html
 */

static void WIN32API halide_InitializeConditionVariable(ConditionVariable *cond)
{
    cond->waiters_count = 0;
    cond->release_count = 0;
    cond->generation_count = 0;
    cond->event = CreateEventA(NULL, true, false, NULL);
    cond->lock = (CriticalSection*) malloc(sizeof(CriticalSection));
    if (!cond->event || !cond->lock)
        return;
    memset(cond->lock, 0, sizeof(CriticalSection));
    InitializeCriticalSection(cond->lock);
}

static void WIN32API halide_WakeAllConditionVariable(ConditionVariable *cond)
{
    EnterCriticalSection(cond->lock);

    if (cond->waiters_count > 0) {
        SetEvent(cond->event);
        cond->release_count = cond->waiters_count;
        cond->generation_count++;
    }

    LeaveCriticalSection(cond->lock);
}

static bool WIN32API halide_SleepConditionVariableCS(ConditionVariable *cond, CriticalSection *cs, uint32_t timeout)
{
    int wait_done;
    int last_waiter;
    int my_generation;

    EnterCriticalSection(cond->lock);

    cond->waiters_count++;
    my_generation = cond->generation_count;

    LeaveCriticalSection(cond->lock);
    LeaveCriticalSection(cs);

    while (true) {
        WaitForSingleObject(cond->event, INFINITE);

        EnterCriticalSection(cond->lock);
        wait_done = (cond->release_count > 0) && (cond->generation_count != my_generation);
        LeaveCriticalSection(cond->lock);

        if (wait_done)
            break;
    }

    EnterCriticalSection(cs);
    EnterCriticalSection(cond->lock);
    cond->waiters_count--;
    cond->release_count--;
    last_waiter = cond->release_count == 0;
    LeaveCriticalSection(cond->lock);

    if (last_waiter)
        ResetEvent(cond->event);

    return true;
}

static bool WIN32API halide_InitOnceExecuteOnce(InitOnce *once, bool (WIN32API * fn)(InitOnce *, void *, void **), void *param, void **context)
{
    while (true)
    {
        switch ((size_t)once->ptr & 3)
        {
            case 2:
                return true;

            case 0:
                if (pInterlockedCompareExchangePointer(&once->ptr, (void*)1, (void*)0) != (void*)0)
                {
                    break;
                }

                if (fn(once, param, context))
                {
                    once->ptr = (void*)2;
                    return true;
                }

                once->ptr = (void*)0;
                return false;

            case 1:
                break;

            default:
                return false;
        }

        Sleep(5);
    }
}

static bool halide_windows_init() {

    if (g_Initialized)
        return true;

    g_kernel32 = LoadLibraryA("kernel32.dll");

    if (g_kernel32)
    {
        pInterlockedCompareExchange = (fnInterlockedCompareExchange) GetProcAddress(g_kernel32, "InterlockedCompareExchange");
        pInterlockedCompareExchange64 = (fnInterlockedCompareExchange64) GetProcAddress(g_kernel32, "InterlockedCompareExchange64");
        pInterlockedCompareExchangePointer = (fnInterlockedCompareExchangePointer) GetProcAddress(g_kernel32, "InterlockedCompareExchangePointer");

        pInitializeConditionVariable = (fnInitializeConditionVariable) GetProcAddress(g_kernel32, "InitializeConditionVariable");
        pWakeAllConditionVariable = (fnWakeAllConditionVariable) GetProcAddress(g_kernel32, "WakeAllConditionVariable");
        pSleepConditionVariableCS = (fnSleepConditionVariableCS) GetProcAddress(g_kernel32, "SleepConditionVariableCS");
        pInitOnceExecuteOnce = (fnInitOnceExecuteOnce) GetProcAddress(g_kernel32, "InitOnceExecuteOnce");
    }

    if (!pInterlockedCompareExchangePointer)
    {
#ifdef BITS_64
        pInterlockedCompareExchangePointer = (fnInterlockedCompareExchangePointer) pInterlockedCompareExchange64;
#else
        pInterlockedCompareExchangePointer = (fnInterlockedCompareExchangePointer) pInterlockedCompareExchange;
#endif
    }

    if (pInitializeConditionVariable && pWakeAllConditionVariable && pSleepConditionVariableCS)
    {
        g_NativeCondVar = true;
    }
    else
    {
        pInitializeConditionVariable = halide_InitializeConditionVariable;
        pWakeAllConditionVariable = halide_WakeAllConditionVariable;
        pSleepConditionVariableCS = halide_SleepConditionVariableCS;
        g_NativeCondVar = false;
    }

    if (pInitOnceExecuteOnce)
    {
        g_NativeInitOnce = true;
    }
    else
    {
        pInitOnceExecuteOnce = halide_InitOnceExecuteOnce;
        g_NativeInitOnce = false;
    }

    g_Initialized = true;

    return true;
}

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
    WaitForSingleObject(thread->handle, INFINITE);
    free(thread);
}

WEAK void halide_mutex_destroy(halide_mutex *mutex_arg) {
    windows_mutex *mutex = (windows_mutex *)mutex_arg;
    if (mutex->once.ptr != 0) {
        DeleteCriticalSection(&mutex->critical_section);
        memset(mutex_arg, 0, sizeof(halide_mutex));
    }
}

WEAK void halide_mutex_lock(halide_mutex *mutex_arg) {
    windows_mutex *mutex = (windows_mutex *)mutex_arg;
    if (!g_Initialized) halide_windows_init();
    pInitOnceExecuteOnce(&mutex->once, init_mutex, mutex, NULL);
    EnterCriticalSection(&mutex->critical_section);
}

WEAK void halide_mutex_unlock(halide_mutex *mutex_arg) {
    windows_mutex *mutex = (windows_mutex *)mutex_arg;
    LeaveCriticalSection(&mutex->critical_section);
}

WEAK void halide_cond_init(struct halide_cond *cond_arg) {
    ConditionVariable *cond = (ConditionVariable *)cond_arg;
    if (!g_Initialized) halide_windows_init();
    pInitializeConditionVariable(cond);
}

WEAK void halide_cond_destroy(struct halide_cond *cond_arg) {
    ConditionVariable *cond = (ConditionVariable *)cond_arg;
    if (!g_Initialized) halide_windows_init();
    // On windows we do not currently destroy condition
    // variables. We're still figuring out mysterious deadlocking
    // issues at process exit.
    if (!g_NativeCondVar)
    {
        CloseHandle(cond->event);
        if (cond->lock) {
            DeleteCriticalSection(cond->lock);
            free(cond->lock);
        }
        memset(cond, 0, sizeof(ConditionVariable));
    }
}

WEAK void halide_cond_broadcast(struct halide_cond *cond_arg) {
    ConditionVariable *cond = (ConditionVariable *)cond_arg;
    if (!g_Initialized) halide_windows_init();
    pWakeAllConditionVariable(cond);
}

WEAK void halide_cond_wait(struct halide_cond *cond_arg, struct halide_mutex *mutex_arg) {
    ConditionVariable *cond = (ConditionVariable *)cond_arg;
    windows_mutex *mutex = (windows_mutex *)mutex_arg;
    if (!g_Initialized) halide_windows_init();
    pSleepConditionVariableCS(cond, &mutex->critical_section, INFINITE);
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
