#include <stdint.h>

#ifndef __APPLE__
#ifdef _LP64
typedef uint64_t size_t;
#else
typedef uint32_t size_t;
#endif
#endif
#define WEAK __attribute__((weak))

extern "C" {

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

    bool Running() { return next < max || active_workers > 0; }
};

// The work queue and thread pool is weak, so one big work queue is shared by all halide functions
#define MAX_THREADS 64
WEAK struct {
    // all fields are protected by this mutex.
    pthread_mutex_t mutex;

    // Singly linked list for FIFO queue.
    work *jobs_head;
    work *jobs_tail;

    // Broadcast whenever items are added to the queue or a job completes.
    pthread_cond_t state_change;
    // Keep track of threads so they can be joined at shutdown
    pthread_t threads[MAX_THREADS];

    // Global flag indicating 
    bool shutdown;

    bool Running() { return !shutdown; }
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

    pthread_mutex_lock(&halide_work_queue.mutex);
    while (owned_job != NULL ? owned_job->Running()
                             : halide_work_queue.Running()) {
        if (halide_work_queue.jobs_head == NULL) {
            pthread_cond_wait(&halide_work_queue.state_change, &halide_work_queue.mutex);
        } else {
            work *job = halide_work_queue.jobs_head;
	    // Claim some tasks
	    //int claimed = (remaining + threads - 1)/threads;
	    int claimed = 1;
	    //fprintf(stderr, "Worker %d: Claiming %d tasks\n", id, claimed);
	    work myjob = *job;
	    job->next += claimed;            
	    if (job->next == job->max) {
		halide_work_queue.jobs_head = job->next_job;
		if (halide_work_queue.jobs_head == NULL) {
		    halide_work_queue.jobs_tail = NULL;
		}
	    } 
	    myjob.max = job->next;
	    job->active_workers++;
	    pthread_mutex_unlock(&halide_work_queue.mutex);
	    for (; myjob.next < myjob.max; myjob.next++) {
		//fprintf(stderr, "Worker %d: Doing job %d\n", id, myjob.next);
		halide_do_task(myjob.f, myjob.next, myjob.closure);
		//fprintf(stderr, "Worker %d: Done with job %d\n", id, myjob.next);
	    }
	    pthread_mutex_lock(&halide_work_queue.mutex);
	    job->active_workers--;
            if (job->next == job->max && job->active_workers == 0 && job != owned_job) {
                pthread_cond_broadcast(&halide_work_queue.state_change);
            }
	}        
    }
    pthread_mutex_unlock(&halide_work_queue.mutex);
    return NULL;
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
        halide_work_queue.jobs_head = halide_work_queue.jobs_tail = NULL;
        char *threadStr = getenv("HL_NUMTHREADS");
        #ifdef _LP64
        // On 64-bit systems we use 8 threads by default
        halide_threads = 8;
        #else
        // On 32-bit systems we use 2 threads by default
        halide_threads = 2;
        #endif
        if (threadStr) {
            halide_threads = atoi(threadStr);
        } else {
            halide_printf("HL_NUMTHREADS not defined. Defaulting to %d threads.\n", halide_threads);
        }
        if (halide_threads > MAX_THREADS) halide_threads = MAX_THREADS;
        for (int i = 0; i < halide_threads-1; i++) {
            //fprintf(stderr, "Creating thread %d\n", i);
            pthread_create(halide_work_queue.threads + i, NULL, halide_worker_thread, NULL);
        }

        halide_thread_pool_initialized = true;
    }

    // Enqueue the job
    pthread_mutex_lock(&halide_work_queue.mutex);
    //fprintf(stderr, "Enqueuing some work\n");
    work job = {NULL, f, min, min + size, closure, 0};
    if (halide_work_queue.jobs_head == NULL) {
        halide_work_queue.jobs_head = &job;
    } else {
        //assert(halide_work_queue.jobs_tail != NULL);
	halide_work_queue.jobs_tail->next_job = &job;
    }
    halide_work_queue.jobs_tail = &job;

    pthread_mutex_unlock(&halide_work_queue.mutex);
    
    //fprintf(stderr, "Waking up workers\n");
    // Wake up everyone
    pthread_cond_broadcast(&halide_work_queue.state_change);

    // Do some work myself
    //fprintf(stderr, "Doing some work on job %d\n", arg.id);
    halide_worker_thread((void *)(&job));    
    //fprintf(stderr, "Parallel for done\n");
}

}
