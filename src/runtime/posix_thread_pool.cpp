#include "HalideRuntime.h"

// TODO: This code currently doesn't work on OS X (Darwin) as we do
// not initialize the pthread_mutex_t using PTHREAD_MUTEX_INITIALIZER
// or pthread_mutex_init. (And Darwin using a non-zero value for the
// siganture here.) Fix is probably to use a pthread_once type
// mechanism to call pthread_mutex_init, but that requires the once
// initializer which might not be zero and is platform dependent. Thus
// we need our own portable once implementation. For now, threadpool
// only works on platforms where PTHREAD_MUTEX_INITIALIZER is zero.

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
    uint64_t _private[6];
} pthread_cond_t;
typedef long pthread_condattr_t;
typedef struct {
    // 64 bytes is enough for a mutex on 64-bit and 32-bit systems
    uint64_t _private[8];
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

WEAK int halide_do_task(void *user_context, halide_task_t f, int idx,
                        uint8_t *closure);

} // extern "C"

namespace Halide { namespace Runtime { namespace Internal {

WEAK int num_threads;
WEAK bool thread_pool_initialized = false;

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
    // all fields are protected by this mutex.
    pthread_mutex_t mutex;

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
    pthread_cond_t wakeup_owners;

    // Broadcast whenever items are added to the work queue.
    pthread_cond_t wakeup_a_team;

    // May also be broadcast when items are added to the work queue if
    // more threads are required than are currently in the A team.
    pthread_cond_t wakeup_b_team;

    // Keep track of threads so they can be joined at shutdown
    pthread_t threads[MAX_THREADS];

    // Global flag indicating
    bool shutdown;

    bool running() {
        return !shutdown;
    }

};
WEAK work_queue_t work_queue;

WEAK int default_do_task(void *user_context, halide_task_t f, int idx,
                        uint8_t *closure) {
    return f(user_context, idx, closure);
}

WEAK void *worker_thread(void *void_arg) {
    work *owned_job = (work *)void_arg;

    // Grab the lock
    pthread_mutex_lock(&work_queue.mutex);

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
                pthread_cond_wait(&work_queue.wakeup_owners, &work_queue.mutex);
            } else if (work_queue.a_team_size <= work_queue.target_a_team_size) {
                // There are no jobs pending. Wait until more jobs are enqueued.
                pthread_cond_wait(&work_queue.wakeup_a_team, &work_queue.mutex);
            } else {
                // There are no jobs pending, and there are too many
                // threads in the A team. Transition to the B team
                // until the wakeup_b_team condition is fired.
                work_queue.a_team_size--;
                pthread_cond_wait(&work_queue.wakeup_b_team, &work_queue.mutex);
                work_queue.a_team_size++;
            }
        } else {
            // Grab the next job.
            work *job = work_queue.jobs;

            // Claim a task from it.
            work myjob = *job;
            job->next++;

            // If there were no more tasks pending for this job,
            // remove it from the stack.
            if (job->next == job->max) {
                work_queue.jobs = job->next_job;
            }

            // Increment the active_worker count so that other threads
            // are aware that this job is still in progress even
            // though there are no outstanding tasks for it.
            job->active_workers++;

            // Release the lock and do the task.
            pthread_mutex_unlock(&work_queue.mutex);
            int result = halide_do_task(myjob.user_context, myjob.f, myjob.next,
                                        myjob.closure);
            pthread_mutex_lock(&work_queue.mutex);

            // If this task failed, set the exit status on the job.
            if (result) {
                job->exit_status = result;
            }

            // We are no longer active on this job
            job->active_workers--;

            // If the job is done and I'm not the owner of it, wake up
            // the owner.
            if (!job->running() && job != owned_job) {
                pthread_cond_broadcast(&work_queue.wakeup_owners);
            }
        }
    }
    pthread_mutex_unlock(&work_queue.mutex);
    return NULL;
}

WEAK int default_do_par_for(void *user_context, halide_task_t f,
                            int min, int size, uint8_t *closure) {
    // Grab the lock. If it hasn't been initialized yet, then the
    // field will be zero-initialized because it's a static
    // global. pthreads helpfully interprets zero-valued mutex objects
    // as uninitialized and initializes them for you (see PTHREAD_MUTEX_INITIALIZER).
    pthread_mutex_lock(&work_queue.mutex);

    if (!thread_pool_initialized) {
        work_queue.shutdown = false;
        pthread_cond_init(&work_queue.wakeup_owners, NULL);
        pthread_cond_init(&work_queue.wakeup_a_team, NULL);
        pthread_cond_init(&work_queue.wakeup_b_team, NULL);
        work_queue.jobs = NULL;

        if (!num_threads) {
            char *threads_str = getenv("HL_NUM_THREADS");
            if (!threads_str) {
                // Legacy name for HL_NUM_THREADS
                threads_str = getenv("HL_NUMTHREADS");
            }
            if (threads_str) {
                num_threads = atoi(threads_str);
            } else {
                num_threads = halide_host_cpu_count();
                // halide_printf(user_context, "HL_NUM_THREADS not defined. Defaulting to %d threads.\n", num_threads);
            }
        }
        if (num_threads > MAX_THREADS) {
            num_threads = MAX_THREADS;
        } else if (num_threads < 1) {
            num_threads = 1;
        }
        for (int i = 0; i < num_threads-1; i++) {
            //fprintf(stderr, "Creating thread %d\n", i);
            pthread_create(work_queue.threads + i, NULL, worker_thread, NULL);
        }
        // Everyone starts on the a team.
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

    pthread_mutex_unlock(&work_queue.mutex);

    // Wake up our A team.
    pthread_cond_broadcast(&work_queue.wakeup_a_team);

    if (wake_b_team) {
        // We need the B team too.
        pthread_cond_broadcast(&work_queue.wakeup_b_team);
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
    void (*f)(void *);
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

WEAK void halide_spawn_thread(void *user_context, void (*f)(void *), void *closure) {
    // Note that we don't pass the user_context through to the
    // thread. It may begin well after the user context is no longer a
    // valid thing.
    pthread_t thread;
    // For the same reason we use malloc instead of
    // halide_malloc. Custom malloc/free overrides may well not behave
    // well if run at unexpected times (e.g. the matching free may
    // occur at static destructor time if the thread never returns).
    spawn_thread_task *t = (spawn_thread_task *)malloc(sizeof(spawn_thread_task));
    t->f = f;
    t->closure = closure;
    pthread_create(&thread, NULL, spawn_thread_helper, t);
}

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
    if (!thread_pool_initialized) return;

    // Wake everyone up and tell them the party's over and it's time
    // to go home
    pthread_mutex_lock(&work_queue.mutex);
    work_queue.shutdown = true;
    pthread_cond_broadcast(&work_queue.wakeup_owners);
    pthread_cond_broadcast(&work_queue.wakeup_a_team);
    pthread_cond_broadcast(&work_queue.wakeup_b_team);
    pthread_mutex_unlock(&work_queue.mutex);

    // Wait until they leave
    for (int i = 0; i < num_threads-1; i++) {
        //fprintf(stderr, "Waiting for thread %d to exit\n", i);
        void *retval;
        pthread_join(work_queue.threads[i], &retval);
    }

    //fprintf(stderr, "All threads have quit. Destroying mutex and condition variable.\n");
    // Tidy up
    pthread_mutex_destroy(&work_queue.mutex);
    // Reinitialize in case we call another do_par_for
    pthread_mutex_init(&work_queue.mutex, NULL);
    pthread_cond_destroy(&work_queue.wakeup_owners);
    pthread_cond_destroy(&work_queue.wakeup_a_team);
    pthread_cond_destroy(&work_queue.wakeup_b_team);
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
