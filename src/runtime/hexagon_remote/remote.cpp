extern "C" {

#include "halide_hexagon_remote.h"
#include <sys/mman.h>
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <qurt.h>
#define FARF_LOW 1
#include "HAP_farf.h"
#include "HAP_power.h"

}

#include "../HalideRuntime.h"

typedef halide_hexagon_remote_handle_t handle_t;
typedef halide_hexagon_remote_buffer buffer;

#define MAX_WORKER_THREADS 4
#define NUM_WORKER_THREADS_TO_CREATE 3
#define STACK_SIZE 4096
#define MAX_WORKLOADS 10
static char stack [MAX_WORKER_THREADS][STACK_SIZE] __attribute__ ((__aligned__(128)));
qurt_sem_t wait_for_work;

struct work {
    work *next_job;
    int (*f)(void *, int, uint8_t *);
    void *user_context;
    int next, max;
    uint8_t *closure;
    int active_workers;
    int exit_status;
    qurt_cond_t wakeup_owner;
    // A job can be in the following states.
    // claimed - When the thread_pool has started work on the entire job, but not necessarily
    //           completed it. Condition: next >= max.
    // running - When there are active workers on the job. Condition: active_workers > 0;
    // done - When the job is completely done and there are no active workers on the job.
    bool claimed() { return next >= max; }
    bool running() { return active_workers > 0; }
    bool done() { return claimed() && !running(); }
};
bool thread_pool_initialized;
struct work_queue_t {
    // all fields are protected by this mutex.
    qurt_mutex_t work_mutex;

    // Jobs that the thread pool needs to work on.
    work jobs[MAX_WORKLOADS];

    // Global flag indicating if the thread pool has been shutdown.
    bool shutdown;

    // The index at which a new job should be added.
    int next_workload;

    // number of jobs in the work queue.
    // To check the status of each job, use the "claimed", "running" and
    // "done" members of each job.
    int num_jobs;

    bool running() {
        return !shutdown;
    }
};
work_queue_t work_queue;
qurt_thread_t threads[MAX_WORKER_THREADS];
int *ptr;;
extern "C" {

void halide_print(void *user_context, const char *str) {
    FARF(LOW, "%s", str);
}

// This is a basic implementation of the Halide runtime for Hexagon.
void halide_error(void *user_context, const char *str) {
    halide_print(user_context, str);
}

void *halide_malloc(void *user_context, size_t x) {
    return memalign(128, x);
}

void halide_free(void *user_context, void *ptr) {
    free(ptr);
}

int halide_do_task(void *user_context, halide_task_t f, int idx,
                   uint8_t *closure) {
    return f(user_context, idx, closure);
}
void lock_work_queue() {
    qurt_mutex_lock(&(work_queue.work_mutex));
}
void unlock_work_queue() {
    qurt_mutex_unlock(&(work_queue.work_mutex));
}
void goto_work(work *owned_job) {
    work myjob;
    // You can work only if you get a lock on the work queue.
    unsigned int tid = qurt_thread_get_id();
    FARF(LOW, "HVX_TP: %d: goto_work: Trying to get a lock on the work queue\n", tid);

    // ***********************
    // *** Lock work queue ***
    // ***********************
    lock_work_queue();
    FARF(LOW, "HVX_TP: %d: goto_work: got a lock on the work queue\n", tid);

    // If I'm a job owner, then I was the thread that called
    // do_par_for, and I should only stay in this function while there
    // are active workers on the job (i.e. running()) or if the job
    // hasn't been claimed entirely (!claimed()). If I'm a lowly worker
    // thread, I should stay in this function as long as the work queue is running.
    while (owned_job != NULL ? (owned_job->running() || !owned_job->claimed())
           : work_queue.running()) {

        FARF(LOW, "HVX_TP: %d: goto_work: In the main goto_work loop \n", tid);
        int num_jobs = work_queue.num_jobs, job_idx;
        work *jobs = work_queue.jobs, *orig_job;
        bool job_found = false;
        if (!owned_job) {
            // If this threads doesn't own a job, it looks for one
            // and tries to do it. If it cannot find a job, it goes
            // to sleep.
            if (num_jobs > 0) {
                for (job_idx = MAX_WORKLOADS-1 ; job_idx >= 0; job_idx--) {
                    if (!jobs[job_idx].claimed()) {
                        myjob = jobs[job_idx];
                        orig_job = &jobs[job_idx];
                        job_found = true;
                        break;
                    }
                }
            }
            if (!job_found) {
                // release the lock and go to sleep.
                // ***************************
                // *** Work queue unlocked ***
                // ***************************
                FARF(LOW, "HVX_TP: %d: goto_work: Couldn't find a job, going to sleep\n", tid);
                unlock_work_queue();
                qurt_sem_down(&wait_for_work);
                FARF(LOW, "HVX_TP: %d: goto_work: Got woken up. Going to look for work.\n", tid);
                 // ***********************
                // *** Lock work queue ***
                // ***********************
                lock_work_queue();
                continue;
            } else {
                FARF(LOW, "HVX_TP: %d: goto_work: Found a job at idx = %d working on x = %d\n", tid, job_idx, myjob.next);
            }
        } else {
            // We are here only if this thread owns a job that is not done.
            // So do a part of it.
            if (owned_job->claimed()) {
                // the owner goes to sleep till the workers wake it up.
                FARF(LOW, "HVX_TP: %d: goto_work: Owner about to sleep\n", tid);
                qurt_cond_wait(&owned_job->wakeup_owner, &work_queue.work_mutex);
                FARF(LOW, "HVX_TP: %d: goto_work: Owner waking up.\n", tid);
                // This thread should have the lock now after having been woken up.
                break;
            } else {
                myjob = *owned_job;
                orig_job = owned_job;
                FARF(LOW, "HVX_TP: %d: goto_work: Owner about to work\n", tid);
            }
        }
        orig_job->next++;
        orig_job->active_workers++;

        // ***************************
        // *** Work queue unlocked ***
        // ***************************
        unlock_work_queue();

        FARF(LOW, "HVX_TP: %d: goto_work: About to do_task, user_context = 0x%x, f = 0x%x x = %d \n", tid, myjob.user_context, myjob.f, myjob.next);
        int result = halide_do_task(myjob.user_context, myjob.f,
                                    myjob.next, myjob.closure);
        FARF(LOW, "HVX_TP: %d: goto_work: Finished do_task with status = %d\n", tid, result);
        // ***********************
        // *** Lock work queue ***
        // ***********************
        lock_work_queue();
        if (result) {
            orig_job->exit_status = result;
        }
        orig_job->active_workers--;
        FARF(LOW, "HVX_TP: %d: goto_work: reduced number of active workers to %d\n", tid);
        if (orig_job->done()) {
            FARF(LOW, "HVX_TP: %d: goto_work: job done\n", tid);
            if (!owned_job) {
                FARF(LOW, "HVX_TP: %d: goto_work: Signalling to owner/master thread that job is done\n", tid);
                qurt_cond_signal(&orig_job->wakeup_owner);
            }
        } else {
            FARF(LOW, "HVX_TP: %d: goto_work: job not yet done\n", tid);
        }
    }
    if (owned_job) {
        work_queue.num_jobs--;
        FARF(LOW, "HVX_TP: %d: goto_work: Owner reducing number of jobs in work queue. New num_jobs = %d\n", tid, work_queue.num_jobs);
    }
    // ***************************
    // *** Work queue unlocked ***
    // ***************************
    unlock_work_queue();
}
void thread_server(void *arg) {
    unsigned int tid = qurt_thread_get_id();
    FARF(LOW, "HVX_TP: %d: In thread_server\n", tid);
    goto_work(NULL);
    FARF(LOW, "HVX_TP: %d: thread_server: Exiting with QURT_EOK.\n", tid);
    qurt_thread_exit(QURT_EOK);
}
void initialize_work_queue() {
    FARF(LOW, "HVX_TP: Master Thread: Initializing work queue\n");
    lock_work_queue();
    work_queue.next_workload = 0;
    work_queue.num_jobs = 0;
    work_queue.shutdown = false;

    for (int i = 0; i < MAX_WORKLOADS; i++) {
        qurt_cond_init(&(work_queue.jobs[i].wakeup_owner));
        work_queue.jobs[i].next = work_queue.jobs[i].max =
            work_queue.jobs[i].active_workers = 0;
    }

    FARF(LOW, "HVX_TP: Master Thread: Work queue initialized\n");
    unlock_work_queue();
}
void create_threads(int num_threads) {
    // Acquire a lock on the work queue.
    FARF(LOW, "HVX_TP: Master Thread: Creating Threads\n");

    int i;
    qurt_thread_attr_t thread_attr;
    for (i = 0; i < num_threads; ++i) {
        qurt_thread_attr_init(&thread_attr);
        qurt_thread_attr_set_stack_addr(&thread_attr, stack[i]);
        qurt_thread_attr_set_stack_size(&thread_attr, 1024);
        qurt_thread_attr_set_priority(&thread_attr, 100);
        qurt_thread_create(&threads[i], &thread_attr, thread_server, (void *)i);
    }
    FARF(LOW, "HVX_TP: Master Thread: Created threads\n");
}
void qurt_thread_pool_init() {
    FARF(LOW, "HVX_TP: Master Thread: Initializing the thread pool\n");
    qurt_mutex_init(&(work_queue.work_mutex));
    qurt_sem_init_val(&wait_for_work, 0);

    initialize_work_queue();

    create_threads(NUM_WORKER_THREADS_TO_CREATE);

    thread_pool_initialized = true;
    FARF(LOW, "HVX_TP: Master Thread: Thread pool initialized\n");
}
int find_empty_job() {
    int i;
    work *jobs = work_queue.jobs;
    for (i = 0; i < MAX_WORKLOADS; ++i) {
        if (jobs[i].done()) {
            return i;
        }
    }
    return -1;
}
int halide_do_par_for(void *user_context, halide_task_t f,
                      int min, int size, uint8_t *closure) {
    FARF(LOW, "HVX_TP: Master Thread: halide_do_par_for\n");

    // 1. If the thread pool hasn't been initialized, initiliaze it.
    // This involves.
    //    a) Creating the threads.
    //    b) Acquiring a lock on the work queue and clearing the jobs therein
    //    c) Setting up a semaphore on which worker threads sleep until awoken
    //       by this thread i.e. the master thread.
    if (!thread_pool_initialized) {
        qurt_thread_pool_init();
    }

    // 2. Lock the work queue again.
    lock_work_queue();

    // 3. Put work in the global work queue.
    int next_workload = work_queue.next_workload;

    if (!work_queue.jobs[next_workload].done()) {
        FARF(LOW, "HVX_TP: Master Thread: Job at idx = %d is still not done, so looking for an empty job\n", next_workload);
        next_workload = find_empty_job();
        if (next_workload == -1) {
            // We should really just be going to sleep here, until woken up by another thread.
            FARF(LOW, "HVX_TP: Master Thread: Work queue is full\n");
            return -1;
        }
        FARF(LOW, "HVX_TP: Master Thread: Found empty job at idx  = %d\n", next_workload);
    }

    FARF(LOW, "HVX_TP: Master Thread: Putting job in the work queue at idx = %d, min = %d, size = %d, user_context = 0x%x, f= 0x%x, \n",
            next_workload, min, size, user_context, f);

    work *job = &work_queue.jobs[next_workload];
    job->f = f;
    job->user_context = user_context;
    job->next = min;          // Start at this index.
    job->max  = min + size;   // Keep going until one less than this index.
    job->closure = closure;   // Use this closure.
    job->exit_status = 0;     // The job hasn't failed yet
    job->active_workers = 0;  // Nobody is working on this yet
    // We are implementing a circular array to store jobs.
    work_queue.next_workload = (next_workload + 1) % MAX_WORKLOADS;
    work_queue.num_jobs++;

    // 4. Unlock global work queue.
    unlock_work_queue();

    // 5. Wake up the other threads in the pool.
    qurt_sem_add(&wait_for_work, size-1);

    // 6. Do some work in the master queue.
    goto_work(job);

    FARF(LOW, "HVX_TP: Master Thread: Finished job\n");
    return 0;
}

void *halide_get_symbol(const char *name) {
    return dlsym(RTLD_DEFAULT, name);
}

void *halide_load_library(const char *name) {
    return dlopen(name, RTLD_LAZY);
}

void *halide_get_library_symbol(void *lib, const char *name) {
    return dlsym(lib, name);
}

const int map_alignment = 4096;

typedef int (*set_runtime_t)(halide_malloc_t user_malloc,
                             halide_free_t custom_free,
                             halide_print_t print,
                             halide_error_handler_t error_handler,
                             halide_do_par_for_t do_par_for,
                             halide_do_task_t do_task,
                             void *(*)(const char *),
                             void *(*)(const char *),
                             void *(*)(void *, const char *));

int context_count = 0;

int halide_hexagon_remote_initialize_kernels(const unsigned char *code, int codeLen,
                                             handle_t *module_ptr) {
#if 1  // Use shared object from file
    const char *filename = (const char *)code;
#else
    const char *filename = "/data/halide_kernels.so";
    FILE* fd = fopen(filename, "w");
    if (!fd) {
        halide_print(NULL, "fopen failed");
        return -1;
    }

    fwrite(code, codeLen, 1, fd);
    fclose(fd);
#endif
    void *lib = dlopen(filename, RTLD_LOCAL | RTLD_LAZY);
    if (!lib) {
        halide_print(NULL, "dlopen failed");
        return -1;
    }

    // Initialize the runtime. The Hexagon runtime can't call any
    // system functions (because we can't link them), so we put all
    // the implementations that need to do so here, and pass poiners
    // to them in here.
    set_runtime_t set_runtime = (set_runtime_t)dlsym(lib, "halide_noos_set_runtime");
    if (!set_runtime) {
        dlclose(lib);
        halide_print(NULL, "halide_noos_set_runtime not found in shared object");
        return -1;
    }

    int result = set_runtime(halide_malloc,
                             halide_free,
                             halide_print,
                             halide_error,
                             halide_do_par_for,
                             halide_do_task,
                             halide_get_symbol,
                             halide_load_library,
                             halide_get_library_symbol);
    if (result != 0) {
        dlclose(lib);
        halide_print(NULL, "set_runtime failed");
        return result;
    }
    *module_ptr = reinterpret_cast<handle_t>(lib);
    halide_print(NULL, "HELLO  HVX, how are ya");
    if (context_count == 0) {
        halide_print(NULL, "Requesting power for HVX...");

        HAP_power_request_t request;

        request.type = HAP_power_set_apptype;
        request.apptype = HAP_POWER_COMPUTE_CLIENT_CLASS;
        int retval = HAP_power_set(NULL, &request);
        if (0 != retval) {
            halide_print(NULL, "HAP_power_set(HAP_power_set_apptype) failed");
            return -1;
        }
/*
        request.type = HAP_power_set_DCVS;
        request.dcvs.dcvs_enable = FALSE;
        request.dcvs.dcvs_option = HAP_DCVS_ADJUST_UP_DOWN;
        retval = HAP_power_set(NULL, &request);
        if (0 != retval) {
            halide_print(NULL, "HAP_power_set(HAP_power_set_DCVS) failed");
            return -1;
        }
*/

        request.type = HAP_power_set_HVX;
        request.hvx.power_up = TRUE;
        retval = HAP_power_set(NULL, &request);
        if (0 != retval) {
            halide_print(NULL, "HAP_power_set(HAP_power_set_HVX) failed");
            return -1;
        }

        request.type = HAP_power_set_mips_bw;
        request.mips_bw.set_mips = TRUE;
        request.mips_bw.mipsPerThread = 500;
        request.mips_bw.mipsTotal = 1000;
        request.mips_bw.set_bus_bw = TRUE;
        request.mips_bw.bwBytePerSec = static_cast<uint64_t>(12000) * 1000000;
        request.mips_bw.busbwUsagePercentage = 100;
        request.mips_bw.set_latency = TRUE;
        request.mips_bw.latency = 1;
        retval = HAP_power_set(NULL, &request);
        if (0 != retval) {
            halide_print(NULL, "HAP_power_set(HAP_power_set_mips_bw) failed");
            return -1;
        }
    }

    context_count++;

    return 0;
}

handle_t halide_hexagon_remote_get_symbol(handle_t module_ptr, const char* name, int nameLen) {
    return reinterpret_cast<handle_t>(dlsym(reinterpret_cast<void*>(module_ptr), name));
}

int halide_hexagon_remote_run(handle_t module_ptr, handle_t function,
                              const buffer *input_buffersPtrs, int input_buffersLen,
                              buffer *output_buffersPtrs, int output_buffersLen,
                              const buffer *input_scalarsPtrs, int input_scalarsLen) {
    // Get a pointer to the argv version of the pipeline.
    typedef int (*pipeline_argv_t)(void **);
    pipeline_argv_t pipeline = reinterpret_cast<pipeline_argv_t>(function);

    // Construct a list of arguments. This is only part of a
    // buffer_t. We know that the only field of buffer_t that the
    // generated code should access is the host field (any other
    // fields should be passed as their own scalar parameters) so we
    // can just make this dummy buffer_t type.
    struct buffer_t {
        uint64_t dev;
        uint8_t* host;
    };
    void **args = (void **)__builtin_alloca((input_buffersLen + input_scalarsLen + output_buffersLen) * sizeof(void *));
    buffer_t *buffers = (buffer_t *)__builtin_alloca((input_buffersLen + output_buffersLen) * sizeof(buffer_t));

    void **next_arg = &args[0];
    buffer_t *next_buffer_t = &buffers[0];
    // Input buffers come first.
    for (int i = 0; i < input_buffersLen; i++, next_arg++, next_buffer_t++) {
        next_buffer_t->host = input_buffersPtrs[i].data;
        *next_arg = next_buffer_t;
    }
    // Output buffers are next.
    for (int i = 0; i < output_buffersLen; i++, next_arg++, next_buffer_t++) {
        next_buffer_t->host = output_buffersPtrs[i].data;
        *next_arg = next_buffer_t;
    }
    // Input scalars are last.
    for (int i = 0; i < input_scalarsLen; i++, next_arg++) {
        *next_arg = input_scalarsPtrs[i].data;
    }

    // Call the pipeline and return the result.
    return pipeline(args);
}

int halide_hexagon_remote_release_kernels(handle_t module_ptr, int codeLen) {
    dlclose(reinterpret_cast<void*>(module_ptr));
    FARF(LOW, "HVX_TP: Master Thread: In halide_hexagon_remote_release_kernels\n");
    if (context_count-- == 0) {
        HAP_power_request(0, 0, -1);
    }
    if (thread_pool_initialized) {

        lock_work_queue();
        work_queue.next_workload = 0;
        work_queue.num_jobs = 0;
        FARF(LOW, "HVX_TP: Master Thread: Shutting down the work queue\n");
        work_queue.shutdown = true;
        qurt_sem_add(&wait_for_work, NUM_WORKER_THREADS_TO_CREATE);
        unlock_work_queue();
        thread_pool_initialized = false;
       for (int i = 0; i < NUM_WORKER_THREADS_TO_CREATE; ++i) {
            int status;
            qurt_thread_join(threads[i], &status);
            if ((status != QURT_EOK) && (status != QURT_ENOTHREAD)) {
                FARF(LOW, "HVX_TP: Master Thread: Thread pool did not shutdown cleanly\n");
            }
        }
       FARF(LOW, "HVX_TP: Master Thread: Thread pool has been shutdown\n");
    } else {
        FARF(LOW, "HVX_TP: Master Thread: Thread pool wasn't initialized\n");
    }
    return 0;
}

}  // extern "C"
