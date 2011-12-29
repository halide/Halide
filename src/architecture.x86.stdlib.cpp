#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <math.h>
#include <pthread.h>
#include <assert.h>

#include "buffer.h"

extern "C" {

#define PTR_OFFSET(ptr,offset)	((void*)((char*)(ptr) + (offset)))

buffer_t* __x86_force_include_buffer_t;

void *safe_malloc(size_t x) {
    void *mem;
    x = ((x + 4095)/4096) * 4096;
    posix_memalign(&mem, 4096, x + 4096 * 3);
    printf("Allocated %lu bytes at %p with an electric fence\n", x, mem);

    // write the end address to unprotect in the initial fence
    ((void **)mem)[0] = PTR_OFFSET(mem, x + 4096);
    
    mprotect(mem, 4096, PROT_NONE);
    mprotect(PTR_OFFSET(mem, x + 4096), 4096, PROT_NONE);
    
    return PTR_OFFSET(mem, 4096);
}

void safe_free(void *ptr) {
    void *start = PTR_OFFSET(ptr, -4096);
    mprotect(start, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
    void *end = ((void **)start)[0];
    mprotect(end, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
    free(start);
}

struct work {
    void (*f)(int, uint8_t *);
    int next, max;
    uint8_t *closure;
    int id;
    int active_workers;
};
/*
void *do_work(void *arg) {
    work *job = (work *)arg;
    printf("Spawning job %d\n", job->idx);
    job->f(job->idx, job->closure);
    printf("Finished job %d\n", job->idx);
    return NULL;
}
*/

#define MAX_JOBS 4096
#define THREADS 8
struct {
    work jobs[MAX_JOBS];
    int head;
    int tail;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_t threads[THREADS];
    int ids;
} work_queue;

struct worker_arg {
    int id;
    work *job;
};

void *worker(void *void_arg) {
    worker_arg *arg = (worker_arg *)void_arg;
    while (1) {
        //fprintf(stderr, "About to lock mutex\n");
        pthread_mutex_lock(&work_queue.mutex);
        //fprintf(stderr, "Mutex locked, checking for work\n");

        // we're master, and there's no more work
        if (arg && arg->job->id != arg->id) {
            // wait until other workers are done
            if (arg->job->active_workers) {
                pthread_mutex_unlock(&work_queue.mutex);
                while (true) {
                    //fprintf(stderr, "Master waiting for workers to finish\n");
                    pthread_mutex_lock(&work_queue.mutex);
                    if (!arg->job->active_workers)
                        break;
                    pthread_mutex_unlock(&work_queue.mutex);
                }
            }
            // job is actually done
            pthread_mutex_unlock(&work_queue.mutex);
            //fprintf(stderr, "My work here is done. I am needed ... elsewhere!\n");
            return NULL;
        }
            
        if (work_queue.head == work_queue.tail) {
            assert(!arg); // the master should never get here
            //fprintf(stderr, "No work left. Going to sleep.\n");

            pthread_cond_wait(&work_queue.not_empty, &work_queue.mutex);
            pthread_mutex_unlock(&work_queue.mutex);
            continue;
        }

        //fprintf(stderr, "There is work\n");
        work *job = work_queue.jobs + work_queue.head;
        if (job->next == job->max) {
            //fprintf(stderr, "Found a finished job. Removing it\n");
            work_queue.head = (work_queue.head + 1) % MAX_JOBS;            
            job->id = 0; // mark the job done
            pthread_mutex_unlock(&work_queue.mutex);
        } else {
            // claim this task
            work myjob = *job;
            job->next++;
            job->active_workers++;
            pthread_mutex_unlock(&work_queue.mutex);
            //fprintf(stderr, "Doing job %d\n", myjob.next);
            myjob.f(myjob.next, myjob.closure);
            //fprintf(stderr, "Done with job %d\n", myjob.next);
            pthread_mutex_lock(&work_queue.mutex);
            job->active_workers--;
            pthread_mutex_unlock(&work_queue.mutex);
        }        
    }
}

void do_par_for(void (*f)(int, uint8_t *), int min, int size, uint8_t *closure) {
    static bool thread_pool_initialized = false;
    if (!thread_pool_initialized) {
        pthread_mutex_init(&work_queue.mutex, NULL);
        pthread_cond_init(&work_queue.not_empty, NULL);
        work_queue.head = work_queue.tail = 0;
        work_queue.ids = 1;
        for (int i = 0; i < THREADS; i++) {
            pthread_create(work_queue.threads + i, NULL, worker, NULL);
        }

        thread_pool_initialized = true;
    }

    // Enqueue the job
    pthread_mutex_lock(&work_queue.mutex);
    //fprintf(stderr, "Enqueuing some work\n");
    work job = {f, min, min + size, closure, work_queue.ids++, 0};
    if (job.id == 0) job.id = work_queue.ids++; // disallow zero, as it flags a completed job
    work_queue.jobs[work_queue.tail] = job;
    work *jobPtr = work_queue.jobs + work_queue.tail;
    worker_arg arg = {job.id, jobPtr};
    int new_tail = (work_queue.tail + 1) % MAX_JOBS;
    assert(new_tail != work_queue.head); 
    work_queue.tail = new_tail;

    // TODO: check to make sure the work queue doesn't overflow
    pthread_mutex_unlock(&work_queue.mutex);
    
    // Wake up everyone
    pthread_cond_broadcast(&work_queue.not_empty);

    // Do some work myself
    worker((void *)(&arg));
}

/*
void do_par_for(void (*f)(int, uint8_t *), int min, int size, uint8_t *closure) {
    // for now, just create a thread for each loop instance
    printf("Creating %d jobs starting at index %d using closure at %p and function at %p\n", size, min, closure, f);
    pthread_t threads[size];
    work jobs[size];
    for (int i = 0; i < size; i++)  {
        jobs[i].f = f;
        jobs[i].idx = i + min;
        jobs[i].closure = closure;
        pthread_create(threads+i, NULL, do_work, (void *)(jobs + i));
    }
    printf("Waiting for threads to finish\n");
    for (int i = 0; i < size; i++) {
        pthread_join(threads[i], NULL);
    }
    printf("Parallel for complete\n");
}
*/

float sqrt_f32(float x) {
    return sqrtf(x);
}

float sin_f32(float x) {
    return sinf(x);
}

float cos_f32(float x) {
    return cosf(x);
}

float exp_f32(float x) {
    return expf(x);
}

float log_f32(float x) {
    return logf(x);
}

float pow_f32(float x, float y) {
    return powf(x, y);
}

float floor_f32(float x) {
    return floorf(x);
}

float ceil_f32(float x) {
    return ceilf(x);
}

float round_f32(float x) {
    return roundf(x);
}

}

