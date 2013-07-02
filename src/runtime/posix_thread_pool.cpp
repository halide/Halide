#include "HalideRuntime.h"

#include <stdint.h>
#include <unistd.h>

#ifndef __APPLE__
#ifdef _LP64
typedef uint64_t size_t;
#else
typedef uint32_t size_t;
#endif
#endif
#define WEAK __attribute__((weak))

extern "C" {

#ifndef __SIZEOF_PTHREAD_ATTR_T

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
    // 40 bytes is enough for a mutex on 64-bit and 32-bit systems
    unsigned char _private[40]; 
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
#else

// We've already included some of pthreads, may as well include it
// all. This is forced by the OpenCL backend, where CL.h includes some
// of pthreads.
#include <pthread.h>

#endif

extern char *getenv(const char *);
extern int atoi(const char *);

extern int halide_printf(const char *, ...);

#ifndef NULL
#define NULL 0
#endif

struct work {
    work *next_job;
    void (*f)(int, uint8_t *);
    int next, max;
    uint8_t *closure;
    int active_workers;

    bool running() { return next < max || active_workers > 0; }
};

// The work queue and thread pool is weak, so one big work queue is shared by all halide functions
#define MAX_THREADS 64
WEAK struct {
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

} halide_work_queue;

WEAK int halide_threads;
WEAK bool halide_thread_pool_initialized = false;

WEAK void halide_shutdown_thread_pool() {
    if (!halide_thread_pool_initialized) return;

    // Wake everyone up and tell them the party's over and it's time
    // to go home
    pthread_mutex_lock(&halide_work_queue.mutex);
    halide_work_queue.shutdown = true;
    pthread_cond_broadcast(&halide_work_queue.state_change);
    pthread_mutex_unlock(&halide_work_queue.mutex);

    // Wait until they leave
    for (int i = 0; i < halide_threads-1; i++) {
        //fprintf(stderr, "Waiting for thread %d to exit\n", i);
        void *retval;
        pthread_join(halide_work_queue.threads[i], &retval);
    }

    //fprintf(stderr, "All threads have quit. Destroying mutex and condition variable.\n");
    // Tidy up
    pthread_mutex_destroy(&halide_work_queue.mutex);
    pthread_cond_destroy(&halide_work_queue.state_change);
    halide_thread_pool_initialized = false;
}

WEAK void (*halide_custom_do_task)(void (*)(int, uint8_t *), int, uint8_t *);
WEAK void halide_set_custom_do_task(void (*f)(void (*)(int, uint8_t *), int, uint8_t *)) {
    halide_custom_do_task = f;
}

WEAK void (*halide_custom_do_par_for)(void (*)(int, uint8_t *), int, int, uint8_t *);
WEAK void halide_set_custom_do_par_for(void (*f)(void (*)(int, uint8_t *), int, int, uint8_t *)) {
    halide_custom_do_par_for = f;
}

WEAK void halide_do_task(void (*f)(int, uint8_t *), int idx, uint8_t *closure) {
    if (halide_custom_do_task) {
        (*halide_custom_do_task)(f, idx, closure);
    } else {
        f(idx, closure);
    }
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
            halide_do_task(myjob.f, myjob.next, myjob.closure);
            pthread_mutex_lock(&halide_work_queue.mutex);
            
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

WEAK int halide_host_cpu_count() {
    return sysconf(_SC_NPROCESSORS_ONLN);
}

WEAK void halide_do_par_for(void (*f)(int, uint8_t *), int min, int size, uint8_t *closure) {
    if (halide_custom_do_par_for) {
        (*halide_custom_do_par_for)(f, min, size, closure);
        return;
    }
    if (!halide_thread_pool_initialized) {
        halide_work_queue.shutdown = false;
        pthread_mutex_init(&halide_work_queue.mutex, NULL);
        pthread_cond_init(&halide_work_queue.state_change, NULL);
        halide_work_queue.jobs = NULL;

        char *threadStr = getenv("HL_NUMTHREADS");
        if (threadStr) {
            halide_threads = atoi(threadStr);
        } else {
            halide_threads = halide_host_cpu_count();
            // halide_printf("HL_NUMTHREADS not defined. Defaulting to %d threads.\n", halide_threads);
        }
        if (halide_threads > MAX_THREADS) {
            halide_threads = MAX_THREADS;
        } else if (halide_threads < 1) {
            halide_threads = 1;
        }
        for (int i = 0; i < halide_threads-1; i++) {
            //fprintf(stderr, "Creating thread %d\n", i);
            pthread_create(halide_work_queue.threads + i, NULL, halide_worker_thread, NULL);
        }

        halide_thread_pool_initialized = true;
    }

    // Make the job.
    work job; 
    job.f = f;               // The job should call this function. It takes an index and a closure.
    job.next = min;          // Start at this index.
    job.max  = min + size;   // Keep going until one less than this index.
    job.closure = closure;   // Use this closure.
    job.active_workers = 0;

    // Push the job onto the stack.
    pthread_mutex_lock(&halide_work_queue.mutex);
    job.next_job = halide_work_queue.jobs;
    halide_work_queue.jobs = &job;
    pthread_mutex_unlock(&halide_work_queue.mutex);
    
    // Wake up any idle worker threads.
    pthread_cond_broadcast(&halide_work_queue.state_change);

    // Do some work myself.
    halide_worker_thread((void *)(&job));    
}

}
