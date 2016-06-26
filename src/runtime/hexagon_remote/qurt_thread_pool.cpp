// TODO: This should be moved to src/runtime eventually.

extern "C" {

#include "bin/src/halide_hexagon_remote.h"
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <qurt.h>

}

#include "../HalideRuntime.h"

#define MAX_WORKER_THREADS 4
#define NUM_WORKER_THREADS_TO_CREATE (MAX_WORKER_THREADS - 1)
#define STACK_SIZE 256 * 1024

static char stack [MAX_WORKER_THREADS][STACK_SIZE] __attribute__ ((__aligned__(128)));

//qurt_cond_t work_in_queue;
struct work {
    work *next_job;
    int (*f)(void *, int, uint8_t *);
    void *user_context;
    int next, end;
    uint8_t *closure;
    int active_workers;
    int exit_status;
    qurt_cond_t wakeup_owner;
    qurt_hvx_mode_t curr_hvx_mode;
    // A job can be in the following states.
    // claimed - When the thread_pool has started work on the entire job, but not necessarily
    //           completed it. Condition: next >= end.
    // running - When there are active workers on the job. Condition: active_workers > 0;
    // done - When the job is completely done and there are no active workers on the job.
    bool claimed() { return next >= end; }
    bool running() { return active_workers > 0; }
    bool done() { return claimed() && !running(); }
};

bool thread_pool_initialized = false;
struct work_queue_t {
    // all fields are protected by this mutex.
    qurt_mutex_t work_mutex;

    qurt_cond_t wakeup_workers;

    // Jobs that the thread pool needs to work on.
    work *jobs;

    // Global flag indicating if the thread pool has been shutdown.
    bool shutdown;

    bool running() {
        return !shutdown;
    }
    void lock() {
        qurt_mutex_lock(&work_mutex);
    }
    void unlock() {
        qurt_mutex_unlock(&work_mutex);
    }
    void init() {
        shutdown = false;
        jobs = NULL;
        qurt_cond_init(&wakeup_workers);
    }
    work_queue_t() {
        // Do not do anything more. We'll initialize the work
        // queue on demand, i.e if the pipeline uses ".parallel"
        qurt_mutex_init(&work_mutex);
    }
};
static work_queue_t work_queue;
qurt_thread_t threads[MAX_WORKER_THREADS];

// This function does the real work of the thread pool.
// owned_job is used to tell the differece between the master thread
// and the worker threads. If owned_job is non-null then it means that
// we are in the master thread, i.e. the thread that put owned_job in
// the work queue. This thread, like other worker threads in this function,
// tries to acquire a lock on the work queue. Each thread then looks for
// a job to do. In the case of the master thread, it is "owned_job" while
// a worker thread has to iterate backwards over the work queue and find
// a job to work on. Once a job has been found, the thread, after some
// book-keeping, releases the lock and call halide_do_task to do the job.
// After the job is done, the lock on the queue is re-acquired to update
// the status and the thread loops again to look for new work. If a worker
// thread doesn't find any new work, it goes to sleep until awoken by
// the master thread.
void worker_thread(work *owned_job) {
    // You can work only if you get a lock on the work queue.
    unsigned int tid = qurt_thread_get_id();

    bool locked = owned_job != NULL;

    // ***********************
    // *** Lock work queue ***
    // ***********************
    work_queue.lock();

    // If I'm a job owner, then I was the thread that called
    // do_par_for, and I should only stay in this function while there
    // are active workers on the job (i.e. running()) or if the job
    // hasn't been claimed entirely (!claimed()). If I'm a lowly worker
    // thread, I should stay in this function as long as the work queue is running.
    while (owned_job != NULL ? (owned_job->running() || !owned_job->claimed())
           : work_queue.running()) {

        work *job = NULL;
        if (!owned_job) {
            // If this threads doesn't own a job, it looks for one
            // and tries to do it. If it cannot find a job, it goes
            // to sleep.
            job = work_queue.jobs;
            if (!job) {
                qurt_cond_wait(&work_queue.wakeup_workers, &work_queue.work_mutex);
                continue;
            }
        } else {
            // We are here only if this thread owns a job that is not done.
            // So do a part of it.
            if (owned_job->claimed()) {
                // the owner goes to sleep till the workers wake it up.
                qurt_cond_wait(&owned_job->wakeup_owner, &work_queue.work_mutex);
                // This thread should have the lock now after having been woken up.
                break;
            } else {
                job = owned_job;
            }
        }
        int myjob = job->next++;
        job->active_workers++;
        // If all tasks of the job have been claimed, then pop the job off the stack.
        if (job->claimed()) {
            work_queue.jobs = job->next_job;
        }
        // ***************************
        // *** Work queue unlocked ***
        // ***************************
        work_queue.unlock();

        if (!locked) {
            int lock_status = qurt_hvx_lock(job->curr_hvx_mode);
            // This isn't exactly the best thing because we are skipping the entire
            // job just because this thread couldn't acquire an HVX lock.
            // On the other hand, it may not be that bad a thing to do because
            // the failure to acquire an hvx lock might indicate something near
            // fatal in the system.
            if (lock_status != QURT_EOK) {
                work_queue.lock();
                job->exit_status = lock_status;
                job->active_workers--;
                if (!job->claimed()) {
                    job->next = job->end;
                    work_queue.jobs = job->next_job;
                }
                continue;
            }
        }

        int result = halide_do_task(job->user_context, job->f,
                                    myjob, job->closure);

        qurt_hvx_unlock();
        locked = false;

        // ***********************
        // *** Lock work queue ***
        // ***********************
        work_queue.lock();
        if (result) {
            job->exit_status = result;
            // If the job has been claimed already work_queue.jobs
            // should have been updated above. Don't do it again.
            if (!job->claimed()) {
                job->next = job->end;
                work_queue.jobs = job->next_job;
            }
        }
        job->active_workers--;
        if (job->done()) {
            if (!owned_job) {
                qurt_cond_signal(&(job->wakeup_owner));
            }
        }
    }
    // ***************************
    // *** Work queue unlocked ***
    // ***************************
    work_queue.unlock();
}

void create_threads(int num_threads) {
    // Acquire a lock on the work queue.
    qurt_thread_attr_t thread_attr;
    for (int i = 0; i < num_threads; ++i) {
        qurt_thread_attr_init(&thread_attr);
        qurt_thread_attr_set_stack_addr(&thread_attr, stack[i]);
        qurt_thread_attr_set_stack_size(&thread_attr, STACK_SIZE);
        qurt_thread_attr_set_priority(&thread_attr, 100);
        qurt_thread_create(&threads[i], &thread_attr, (void (*)(void *))worker_thread, NULL);
    }
}

void qurt_thread_pool_init() {
    work_queue.init();
    create_threads(NUM_WORKER_THREADS_TO_CREATE);
    thread_pool_initialized = true;
}

extern "C" {

int halide_do_par_for(void *user_context, halide_task_t f,
                      int min, int size, uint8_t *closure) {

    // 1. Lock the work queue.
    // We lock the work queue before we initialize the thread pool,
    // thereby ensuring that the thread pool is initialized by only one
    // thread.
    work_queue.lock();

    // 2. If the thread pool hasn't been initialized, initiliaze it.
    // This involves.
    //    a) Creating the threads.
    //    b) Acquiring a lock on the work queue and clearing the jobs therein
    //    c) Setting up a semaphore on which worker threads sleep until awoken
    //       by this thread i.e. the master thread.
    if (!thread_pool_initialized) {
        // Initialize the work queue mutex.
        // lock work queue
        //    wq.shutdown = false;
        //    wq.jobs = NULL;
        // unlcok work queue
        // create NUM_WORKER_THREADS_TO_CREATE number of threads.
        // thread_pool_initialized = true;
        qurt_thread_pool_init();
    }


    // 3. Put work in the global work queue.
    work job;
    job.f = f;
    job.user_context = user_context;
    job.next = min;          // Start at this index.
    job.end  = min + size;   // Keep going until one less than this index.
    job.closure = closure;   // Use this closure.
    job.exit_status = 0;     // The job hasn't failed yet
    job.active_workers = 0;  // Nobody is working on this yet
    qurt_cond_init(&job.wakeup_owner);
    job.next_job = work_queue.jobs;
    job.curr_hvx_mode = (qurt_hvx_mode_t) qurt_hvx_get_mode();
    work_queue.jobs = &job;

    // 4. Wake up the other threads in the pool.
    qurt_cond_signal(&work_queue.wakeup_workers);

    // 5. Unlock global work queue.
    work_queue.unlock();

    // 6. Do some work in the master queue.
    worker_thread(&job);

    qurt_cond_destroy(&job.wakeup_owner);

    qurt_hvx_lock(job.curr_hvx_mode);

    return job.exit_status;
}

void halide_shutdown_thread_pool() {
    if (!thread_pool_initialized) return;

    work_queue.lock();
    work_queue.jobs = NULL;
    work_queue.shutdown = true;
    qurt_cond_signal(&work_queue.wakeup_workers);
    work_queue.unlock();
    thread_pool_initialized = false;
    for (int i = 0; i < NUM_WORKER_THREADS_TO_CREATE; ++i) {
        int status;
        qurt_thread_join(threads[i], &status);
    }
    qurt_mutex_destroy(&work_queue.work_mutex);
    qurt_cond_destroy(&work_queue.wakeup_workers);
}

}  // extern "C"
