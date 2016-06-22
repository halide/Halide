#include "HalideRuntime.h"

extern "C" {

#ifdef BITS_64
#define WIN32API
#else
#define WIN32API __stdcall
#endif

extern char *getenv(const char *);
extern int atoi(const char *);

#ifndef NULL
#define NULL 0
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

WEAK int halide_do_task(void *user_context, halide_task_t f, int idx,
                        uint8_t *closure);

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

struct work {
    work *next_job;
    int (*f)(void *, int, uint8_t *);
    void *user_context;
    int next, max;
    uint8_t *closure;
    int active_workers;
    int exit_status;
    bool running() { return next < max || active_workers > 0; }
};

// The work queue and thread pool is weak, so one big work queue is shared by all halide functions
#define MAX_THREADS 64
struct work_queue_t {
    // Initialization of the critical section is guarded by this
    InitOnce init_once;

    // all fields are protected by this mutex.
    CriticalSection mutex;

    // Singly linked list for job stack
    work *jobs;

    // Worker threads are divided into an 'A' team and a 'B' team. The
    // B team sleeps on the wakeup_b_team condition variable. The A
    // team does work. Threads transition to the B team if they wake
    // up and find that a_team_size > target_a_team_size.  Threads
    // move into the A team whenever they wake up and find that
    // a_team_size < target_a_team_size.
    int a_team_size, target_a_team_size;

    // Broadcast when a job completes.
    ConditionVariable wakeup_owners;

    // Broadcast whenever items are added to the work queue.
    ConditionVariable wakeup_a_team;

    // May also be broadcast when items are added to the work queue if
    // more threads are required than are currently in the A team.
    ConditionVariable wakeup_b_team;

    // Keep track of threads so they can be joined at shutdown
    Thread threads[MAX_THREADS];

    // Global flag indicating
    bool shutdown;

    bool running() {
        return !shutdown;
    }

};

WEAK work_queue_t work_queue;

WEAK bool WIN32API InitOnceCallback(InitOnce *, void *, void **) {
    InitializeCriticalSection(&work_queue.mutex);
    return true;
}

WEAK int num_threads;
WEAK bool thread_pool_initialized = false;

WEAK int default_do_task(void *user_context, halide_task_t f, int idx,
                         uint8_t *closure) {
    return f(user_context, idx, closure);
}

WEAK void *worker_thread(void *void_arg) {
    work *owned_job = (work *)void_arg;

    // Grab the lock
    EnterCriticalSection(&work_queue.mutex);

    // If I'm a job owner, then I was the thread that called
    // do_par_for, and I should only stay in this function until my
    // job is complete. If I'm a lowly worker thread, I should stay in
    // this function as long as the work queue is running.
    while (owned_job != NULL ? owned_job->running()
           : work_queue.running()) {

        if (work_queue.jobs == NULL) {
            if (owned_job) {
                // There are no jobs pending. Wait for the last worker
                // to signal that the job is finished.
                SleepConditionVariableCS(&work_queue.wakeup_owners, &work_queue.mutex, -1);
            } else if (work_queue.a_team_size <= work_queue.target_a_team_size) {
                // There are no jobs pending. Wait until more jobs are enqueued.
                SleepConditionVariableCS(&work_queue.wakeup_a_team, &work_queue.mutex, -1);
            } else {
                // There are no jobs pending, and there are too many
                // threads in the A team. Transition to the B team
                // until the wakeup_b_team condition is fired.
                work_queue.a_team_size--;
                SleepConditionVariableCS(&work_queue.wakeup_b_team, &work_queue.mutex, -1);
                work_queue.a_team_size++;
            }
        } else {
            // There are jobs still to do. Grab the next one.
            work *job = work_queue.jobs;

            // Claim a task from it.
            work myjob = *job;
            job->next++;

            // If there were no more tasks pending for this job, or if
            // it has failed, remove it from the stack.
            if (job->next == job->max) {
                work_queue.jobs = job->next_job;
            }

            // Increment the active_worker count so that other threads
            // are aware that this job is still in progress even
            // though there are no outstanding tasks for it.
            job->active_workers++;

            // Release the lock and do the task.
            LeaveCriticalSection(&work_queue.mutex);
            int result = halide_do_task(myjob.user_context, myjob.f, myjob.next,
                                        myjob.closure);
            EnterCriticalSection(&work_queue.mutex);

            // If this task failed, set the exit status on the job.
            if (result) {
                job->exit_status = result;
            }

            // We are no longer active on this job
            job->active_workers--;

            // If the job is done and I'm not the owner of it, wake up
            // the owner.
            if (!job->running() && job != owned_job) {
                WakeAllConditionVariable(&work_queue.wakeup_owners);
            }
        }
    }
    // halide_printf(NULL, "Worker quitting\n");
    LeaveCriticalSection(&work_queue.mutex);
    return NULL;
}

WEAK int default_do_par_for(void *user_context, int (*f)(void *, int, uint8_t *),
                           int min, int size, uint8_t *closure) {
    // halide_printf(user_context, "In do_par_for\n");

    // Create the mutex
    InitOnceExecuteOnce(&work_queue.init_once, InitOnceCallback, NULL, NULL);

    // halide_printf(user_context, "Grabbing mutex\n");

    // Grab it
    EnterCriticalSection(&work_queue.mutex);

    if (!thread_pool_initialized) {
        work_queue.shutdown = false;

        //  halide_printf(user_context, "Making condition variable\n");

        InitializeConditionVariable(&work_queue.wakeup_owners);
        InitializeConditionVariable(&work_queue.wakeup_a_team);
        InitializeConditionVariable(&work_queue.wakeup_b_team);
        work_queue.jobs = NULL;

        if (!num_threads) {
            char *threadStr = getenv("HL_NUM_THREADS");
            if (!threadStr) {
                // Legacy name
                threadStr = getenv("HL_NUMTHREADS");
            }
            if (!threadStr) {
                threadStr = getenv("NUMBER_OF_PROCESSORS"); // Apparently a standard windows environment variable
            }
            if (threadStr) {
                num_threads = atoi(threadStr);
            } else {
                num_threads = 8;
                // halide_printf(user_context, "HL_NUM_THREADS not defined. Defaulting to %d threads.\n", num_threads);
            }
        }
        if (num_threads > MAX_THREADS) {
            num_threads = MAX_THREADS;
        } else if (num_threads < 1) {
            num_threads = 1;
        }
        for (int i = 0; i < num_threads-1; i++) {
            // halide_printf(user_context, "Creating thread %d\n", i);
            work_queue.threads[i] = CreateThread(NULL, 0, worker_thread, NULL, 0, NULL);
        }

        work_queue.a_team_size = num_threads;

        thread_pool_initialized = true;
    }

    // Make the job.
    work job;
    job.f = f;               // The job should call this function. It takes an index and a closure.
    job.user_context = user_context;
    job.next = min;          // Start at this index.
    job.max  = min + size;   // Keep going until one less than this index.
    job.closure = closure;   // Use this closure.
    job.exit_status = 0;     // The job hasn't failed yet
    job.active_workers = 0;  // Nobody is working on this yet

    if (!work_queue.jobs && size < num_threads) {
        // If there's no nested parallelism happening and there are
        // fewer tasks to do than threads, then set the target A team
        // size so that some threads will put themselves to sleep
        // until a larger job arrives.
        work_queue.target_a_team_size = size;
    } else {
        work_queue.target_a_team_size = num_threads;
    }

    // If there are more tasks than threads in the A team, we should
    // wake up everyone.
    bool wake_b_team = size > work_queue.a_team_size;

    // Push the job onto the stack.
    job.next_job = work_queue.jobs;
    work_queue.jobs = &job;

    LeaveCriticalSection(&work_queue.mutex);

    // Wake up our A team.
    WakeAllConditionVariable(&work_queue.wakeup_a_team);

    if (wake_b_team) {
        // We need the B team too.
        WakeAllConditionVariable(&work_queue.wakeup_b_team);
    }

    // Do some work myself.
    worker_thread((void *)(&job));

    // Return zero if the job succeeded, otherwise return the exit
    // status of one of the failing jobs (whichever one failed last).
    return job.exit_status;
}

WEAK halide_do_task_t custom_do_task = default_do_task;
WEAK halide_do_par_for_t custom_do_par_for = default_do_par_for;

struct spawn_thread_task {
    void(*f)(void *);
    void *closure;
};
WEAK void *spawn_thread_helper(void *arg) {
    spawn_thread_task *t = (spawn_thread_task *)arg;
    t->f(t->closure);
    free(t);
    return NULL;
}

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK void halide_spawn_thread(void *user_context, void(*f)(void *), void *closure) {
    spawn_thread_task *t = (spawn_thread_task *)malloc(sizeof(spawn_thread_task));
        t->f = f;
    t->closure = closure;
        CreateThread(NULL, 0, spawn_thread_helper, t, 0, NULL);
}

WEAK void halide_mutex_cleanup(halide_mutex *mutex_arg) {
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

WEAK void halide_shutdown_thread_pool() {
    if (!thread_pool_initialized) return;

    // Wake everyone up and tell them the party's over and it's time
    // to go home
    EnterCriticalSection(&work_queue.mutex);
    work_queue.shutdown = true;
    WakeAllConditionVariable(&work_queue.wakeup_owners);
    WakeAllConditionVariable(&work_queue.wakeup_a_team);
    WakeAllConditionVariable(&work_queue.wakeup_b_team);
    LeaveCriticalSection(&work_queue.mutex);

    // Wait until they leave
    for (int i = 0; i < num_threads-1; i++) {
        WaitForSingleObject(work_queue.threads[i], -1);
    }

    // Tidy up
    DeleteCriticalSection(&work_queue.mutex);
    work_queue.init_once = 0;

    // Condition variables aren't destroyed on windows.
    // DestroyConditionVariable(&work_queue.wakeup_owners);
    // DestroyConditionVariable(&work_queue.wakeup_a_team);
    // DestroyConditionVariable(&work_queue.wakeup_b_team);
    thread_pool_initialized = false;
}

namespace {
__attribute__((destructor))
WEAK void halide_posix_thread_pool_cleanup() {
    halide_shutdown_thread_pool();
}
}

WEAK void halide_set_num_threads(int n) {
    if (num_threads == n) {
        return;
    }

    if (thread_pool_initialized) {
        halide_shutdown_thread_pool();
    }

    num_threads = n;
}

WEAK halide_do_task_t halide_set_custom_do_task(halide_do_task_t f) {
    halide_do_task_t result = custom_do_task;
    custom_do_task = f;
    return result;
}

WEAK halide_do_par_for_t halide_set_custom_do_par_for(halide_do_par_for_t f) {
    halide_do_par_for_t result = custom_do_par_for;
    custom_do_par_for = f;
    return result;
}

WEAK int halide_do_task(void *user_context, halide_task_t f, int idx,
                        uint8_t *closure) {
    return (*custom_do_task)(user_context, f, idx, closure);
}

WEAK int halide_do_par_for(void *user_context, halide_task_t f,
                           int min, int size, uint8_t *closure) {
  return (*custom_do_par_for)(user_context, f, min, size, closure);
}

} // extern "C"
