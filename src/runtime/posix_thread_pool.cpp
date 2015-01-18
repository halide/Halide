#include "runtime_internal.h"

#include "HalideRuntime.h"

// TODO: This code currently doesn't work on OS X (Darwin) as we do
// not initialize the pthread_mutex_t using PTHREAD_MUTEX_INITIALIZER
// or pthread_mutex_init. (And Darwin using a non-zero value for the
// siganture here.) Fix is probably to use a pthread_once type
// mechanism to call pthread_mutex_init, but that requires the once
// initializer which might not be zero and is platform dependent. Thus
// we need our own portable once implementation. For now, threadpool
// only works on platforms where PTHREAD_MUTEX_INITIALIZER is zero.

typedef int (*halide_task)(void *user_context, int, uint8_t *);

extern "C" {

extern long sysconf(int);

typedef struct {
    uint32_t flags;
    void * stack_base;
    size_t stack_size;
    size_t guard_size;
    int32_t sched_policy;
    int32_t sched_priority;
} pthread_attr_t;
typedef long pthread_t;
typedef struct {
    // 48 bytes is enough for a cond on 64-bit and 32-bit systems
    unsigned char _private[48];
} pthread_cond_t;
typedef long pthread_condattr_t;
typedef struct {
    // 64 bytes is enough for a mutex on 64-bit and 32-bit systems
    unsigned char _private[64];
} pthread_mutex_t;
typedef long pthread_mutexattr_t;
extern int pthread_create(pthread_t *thread, pthread_attr_t const * attr,
                          void *(*start_routine)(void *), void * arg);
extern int pthread_join(pthread_t thread, void **retval);
extern int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
extern int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
extern int pthread_cond_broadcast(pthread_cond_t *cond);
extern int pthread_cond_destroy(pthread_cond_t *cond);
extern int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
extern int pthread_mutex_lock(pthread_mutex_t *mutex);
extern int pthread_mutex_unlock(pthread_mutex_t *mutex);
extern int pthread_mutex_destroy(pthread_mutex_t *mutex);

extern char *getenv(const char *);
extern int atoi(const char *);

extern int halide_host_cpu_count();

WEAK int halide_do_task(void *user_context, halide_task f, int idx,
                        uint8_t *closure);

} // extern "C"

namespace Halide { namespace Runtime { namespace Internal {

WEAK int halide_num_threads;
WEAK bool halide_thread_pool_initialized = false;

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
struct halide_work_queue_t {
    // all fields are protected by this mutex.
    pthread_mutex_t mutex;

    // Singly linked list for job stack
    work *jobs;

    // Broadcast whenever items are added to the queue or a job completes.
    pthread_cond_t state_change;
    // Keep track of threads so they can be joined at shutdown
    pthread_t threads[MAX_THREADS];

    // Global flag indicating
    bool shutdown;

    bool running() {
        return !shutdown;
    }

};
WEAK halide_work_queue_t halide_work_queue;

WEAK int default_do_task(void *user_context, halide_task f, int idx,
                        uint8_t *closure) {
    return f(user_context, idx, closure);
}

WEAK void *halide_worker_thread(void *void_arg) {
    work *owned_job = (work *)void_arg;

    // Grab the lock
    pthread_mutex_lock(&halide_work_queue.mutex);

    // If I'm a job owner, then I was the thread that called
    // do_par_for, and I should only stay in this function until my
    // job is complete. If I'm a lowly worker thread, I should stay in
    // this function as long as the work queue is running.
    while (owned_job != NULL ? owned_job->running()
           : halide_work_queue.running()) {

        if (halide_work_queue.jobs == NULL) {
            // There are no jobs pending, though some tasks may still
            // be in flight from the last job. Release the lock and
            // wait for something new to happen.
            pthread_cond_wait(&halide_work_queue.state_change, &halide_work_queue.mutex);
        } else {
            // There are jobs still to do. Grab the next one.
            work *job = halide_work_queue.jobs;

            // Claim a task from it.
            work myjob = *job;
            job->next++;

            // If there were no more tasks pending for this job,
            // remove it from the stack.
            if (job->next == job->max) {
                halide_work_queue.jobs = job->next_job;
            }

            // Increment the active_worker count so that other threads
            // are aware that this job is still in progress even
            // though there are no outstanding tasks for it.
            job->active_workers++;

            // Release the lock and do the task.
            pthread_mutex_unlock(&halide_work_queue.mutex);
            int result = halide_do_task(myjob.user_context, myjob.f, myjob.next,
                                        myjob.closure);
            pthread_mutex_lock(&halide_work_queue.mutex);

            // If this task failed, set the exit status on the job.
            if (result) {
                job->exit_status = result;
            }

            // We are no longer active on this job
            job->active_workers--;

            // If the job is done and I'm not the owner of it, wake up
            // the owner.
            if (!job->running() && job != owned_job) {
                pthread_cond_broadcast(&halide_work_queue.state_change);
            }
        }
    }
    pthread_mutex_unlock(&halide_work_queue.mutex);
    return NULL;
}

WEAK int default_do_par_for(void *user_context, halide_task f,
                            int min, int size, uint8_t *closure) {
    // Grab the lock. If it hasn't been initialized yet, then the
    // field will be zero-initialized because it's a static
    // global. pthreads helpfully interprets zero-valued mutex objects
    // as uninitialized and initializes them for you (see PTHREAD_MUTEX_INITIALIZER).
    pthread_mutex_lock(&halide_work_queue.mutex);

    if (!halide_thread_pool_initialized) {
        halide_work_queue.shutdown = false;
        pthread_cond_init(&halide_work_queue.state_change, NULL);
        halide_work_queue.jobs = NULL;

        if (!halide_num_threads) {
            char *threads_str = getenv("HL_NUM_THREADS");
            if (!threads_str) {
                // Legacy name for HL_NUM_THREADS
                threads_str = getenv("HL_NUMTHREADS");
            }
            if (threads_str) {
                halide_num_threads = atoi(threads_str);
            } else {
                halide_num_threads = halide_host_cpu_count();
                // halide_printf(user_context, "HL_NUM_THREADS not defined. Defaulting to %d threads.\n", halide_num_threads);
            }
        }
        if (halide_num_threads > MAX_THREADS) {
            halide_num_threads = MAX_THREADS;
        } else if (halide_num_threads < 1) {
            halide_num_threads = 1;
        }
        for (int i = 0; i < halide_num_threads-1; i++) {
            //fprintf(stderr, "Creating thread %d\n", i);
            pthread_create(halide_work_queue.threads + i, NULL, halide_worker_thread, NULL);
        }

        halide_thread_pool_initialized = true;
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

    // Push the job onto the stack.
    job.next_job = halide_work_queue.jobs;
    halide_work_queue.jobs = &job;
    pthread_mutex_unlock(&halide_work_queue.mutex);

    // Wake up any idle worker threads.
    pthread_cond_broadcast(&halide_work_queue.state_change);

    // Do some work myself.
    halide_worker_thread((void *)(&job));

    // Return zero if the job succeeded, otherwise return the exit
    // status of one of the failing jobs (whichever one failed last).
    return job.exit_status;
}

WEAK int (*halide_custom_do_task)(void *user_context, halide_task, int, uint8_t *) = default_do_task;
WEAK int (*halide_custom_do_par_for)(void *, halide_task, int, int, uint8_t *) = default_do_par_for;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK void halide_mutex_cleanup(halide_mutex *mutex_arg) {
    pthread_mutex_t *mutex = (pthread_mutex_t *)mutex_arg;
    pthread_mutex_destroy(mutex);
    memset(mutex_arg, 0, sizeof(halide_mutex));
}

WEAK void halide_mutex_lock(halide_mutex *mutex_arg) {
    pthread_mutex_t *mutex = (pthread_mutex_t *)mutex_arg;
    pthread_mutex_lock(mutex);
}

WEAK void halide_mutex_unlock(halide_mutex *mutex_arg) {
    pthread_mutex_t *mutex = (pthread_mutex_t *)mutex_arg;
    pthread_mutex_unlock(mutex);
}


WEAK void halide_shutdown_thread_pool() {
    if (!halide_thread_pool_initialized) return;

    // Wake everyone up and tell them the party's over and it's time
    // to go home
    pthread_mutex_lock(&halide_work_queue.mutex);
    halide_work_queue.shutdown = true;
    pthread_cond_broadcast(&halide_work_queue.state_change);
    pthread_mutex_unlock(&halide_work_queue.mutex);

    // Wait until they leave
    for (int i = 0; i < halide_num_threads-1; i++) {
        //fprintf(stderr, "Waiting for thread %d to exit\n", i);
        void *retval;
        pthread_join(halide_work_queue.threads[i], &retval);
    }

    //fprintf(stderr, "All threads have quit. Destroying mutex and condition variable.\n");
    // Tidy up
    pthread_mutex_destroy(&halide_work_queue.mutex);
    // Reinitialize in case we call another do_par_for
    pthread_mutex_init(&halide_work_queue.mutex, NULL);
    pthread_cond_destroy(&halide_work_queue.state_change);
    halide_thread_pool_initialized = false;
}

namespace {
__attribute__((destructor))
WEAK void halide_posix_thread_pool_cleanup() {
    halide_shutdown_thread_pool();
}
}

WEAK void halide_set_num_threads(int n) {
    if (halide_num_threads == n) {
        return;
    }

    if (halide_thread_pool_initialized) {
        halide_shutdown_thread_pool();
    }

    halide_num_threads = n;
}

WEAK int (*halide_set_custom_do_task(int (*f)(void *, halide_task, int, uint8_t *)))
          (void *, halide_task, int, uint8_t *) {
    int (*result)(void *, halide_task, int, uint8_t *) = halide_custom_do_task;
    halide_custom_do_task = f;
    return result;
}


WEAK int (*halide_set_custom_do_par_for(int (*f)(void *, halide_task, int, int, uint8_t *)))
          (void *, halide_task, int, int, uint8_t *) {
    int (*result)(void *, halide_task, int, int, uint8_t *) = halide_custom_do_par_for;
    halide_custom_do_par_for = f;
    return result;
}

WEAK int halide_do_task(void *user_context, halide_task f, int idx,
                        uint8_t *closure) {
    return (*halide_custom_do_task)(user_context, f, idx, closure);
}

WEAK int halide_do_par_for(void *user_context, int (*f)(void *, int, uint8_t *),
                           int min, int size, uint8_t *closure) {
  return (*halide_custom_do_par_for)(user_context, f, min, size, closure);
}

} // extern "C"
