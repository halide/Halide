#include "HalideRuntime.h"
#include "runtime_atomics.h"
#include "runtime_internal.h"

constexpr int MAX_THREADS = 256;

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
extern WIN32API uint32_t TlsAlloc();
extern WIN32API void *TlsGetValue(uint32_t);
extern WIN32API int32_t TlsSetValue(uint32_t, void *);
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

WEAK uint32_t halide_thread_id_key;
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
            halide_thread_id_key = TlsAlloc();
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
    TlsSetValue(halide_thread_id_key, (void *)(intptr_t)id);
}

struct spawned_thread {
    void (*f)(void *);
    void *closure;
    Thread handle;
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

    intptr_t id = (intptr_t)TlsGetValue(halide_thread_id_key);
    if (id == 0) {
        id = allocate_halide_thread_id();
        TlsSetValue(halide_thread_id_key, (void *)id);
    }
    return (int32_t)id;
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

WEAK halide_thread *halide_spawn_thread(void (*f)(void *), void *closure) {
    spawned_thread *t = (spawned_thread *)malloc(sizeof(spawned_thread));
    t->f = f;
    t->closure = closure;
    t->thread_id = allocate_halide_thread_id();
    t->handle = CreateThread(nullptr, 0, spawn_thread_helper, t, 0, nullptr);
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
    bool should_park = false;

    thread_parker(const thread_parker &) = delete;
    thread_parker &operator=(const thread_parker &) = delete;
    thread_parker(thread_parker &&) = delete;
    thread_parker &operator=(thread_parker &&) = delete;

    ALWAYS_INLINE thread_parker() {
        InitializeCriticalSection(&critical_section);
        InitializeConditionVariable(&condvar);
    }

    ALWAYS_INLINE ~thread_parker() {
        // Windows ConditionVariable objects do not need to be deleted. There is no API to do so.
        DeleteCriticalSection(&critical_section);
    }

    ALWAYS_INLINE void prepare_park() {
        should_park = true;
    }

    ALWAYS_INLINE void park() {
        EnterCriticalSection(&critical_section);
        while (should_park) {
            SleepConditionVariableCS(&condvar, &critical_section, -1);
        }
        LeaveCriticalSection(&critical_section);
    }

    ALWAYS_INLINE void unpark_start() {
        EnterCriticalSection(&critical_section);
    }

    ALWAYS_INLINE void unpark() {
        should_park = false;
        WakeConditionVariable(&condvar);
    }

    ALWAYS_INLINE void unpark_finish() {
        LeaveCriticalSection(&critical_section);
    }
};

}  // namespace Synchronization
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#include "synchronization_common.h"

#include "thread_pool_common.h"
