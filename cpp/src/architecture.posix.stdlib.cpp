#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <assert.h>
#include <cfloat>
#include <stdarg.h>

#include "buffer_t.h"


extern "C" {

#define PTR_OFFSET(ptr,offset)	((void*)((char*)(ptr) + (offset)))

// The functions below are all weak or inline so that you can link
// multiple halide modules without name conflicts. For the inline ones
// we need to add attribute((used)) so that clang doesn't strip them
// out.

#define WEAK __attribute__((weak))
#define INLINE inline __attribute__((used)) __attribute__((always_inline)) __attribute__((nothrow))

// This only gets defined if it's not already defined by an including module, e.g. PTX.
// This also serves to forcibly include the buffer_t struct definition
#ifndef _COPY_TO_HOST
#define _COPY_TO_HOST
WEAK void __copy_to_host(buffer_t* buf) { /* NOP */ }
#endif //_COPY_TO_HOST

static void *(*halide_custom_malloc)(size_t) = NULL;
static void (*halide_custom_free)(void *) = NULL;
WEAK void set_custom_allocator(void *(*cust_malloc)(size_t), void (*cust_free)(void *)) {
    halide_custom_malloc = cust_malloc;
    halide_custom_free = cust_free;
}

WEAK void *fast_malloc(size_t x) {
    if (halide_custom_malloc) {
        return halide_custom_malloc(x);
    } else {
        void *orig = malloc(x+32);
        // Round up to next multiple of 32. Should add at least 8 bytes so we can fit the original pointer.
        void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
        ((void **)ptr)[-1] = orig;
        return ptr;
    }
}

WEAK void fast_free(void *ptr) {
    if (halide_custom_free) {
        halide_custom_free(ptr);
    } else {
        free(((void**)ptr)[-1]);
    }
}

WEAK int hlprintf(const char * fmt, ...) {
    va_list args;
    va_start(args,fmt);
    int ret = vfprintf(stderr, fmt, args);
    va_end(args);
    return ret;
}

static void (*halide_error_handler)(char *) = NULL;

WEAK void halide_error(char *msg) {
    if (halide_error_handler) (*halide_error_handler)(msg);
    else {        
        fprintf(stderr, "Error: %s\n", msg);
        exit(1);
    }
}

WEAK void set_error_handler(void (*handler)(char *)) {
    halide_error_handler = handler;
}

WEAK int32_t debug_to_file(const char *filename, uint8_t *data, 
                           int32_t s0, int32_t s1, int32_t s2, int32_t s3, 
                           int32_t type_code, int32_t bytes_per_element) {
    FILE *f = fopen(filename, "wb");
    if (!f) return -1;
    size_t elts = s0;
    elts *= s1*s2*s3;
    int32_t header[] = {s0, s1, s2, s3, type_code};
    size_t written = fwrite((void *)(&header[0]), 4, 5, f);
    if (written != 5) return -2;
    written = fwrite((void *)data, bytes_per_element, elts, f);    
    fclose(f);
    if (written == elts) return 0;
    else return int(written)+1;
}

struct work {
    void (*f)(int, uint8_t *);
    int next, max;
    uint8_t *closure;
    int id;
    int active_workers;
};

// The work queue and thread count are static, which means each halide
// function gets a unique one. Is this a good idea?
#define MAX_JOBS 65536
#define MAX_THREADS 64
static struct {
    work jobs[MAX_JOBS];
    int head;
    int tail;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_t threads[MAX_THREADS];
    int ids;
    bool shutdown;
} work_queue;

struct worker_arg {
    int id;
    work *job;
};

static int threads;
static bool thread_pool_initialized = false;

WEAK void shutdown_thread_pool() {
    if (!thread_pool_initialized) return;

    // Wake everyone up and tell them the party's over and it's time
    // to go home
    pthread_mutex_lock(&work_queue.mutex);
    work_queue.shutdown = true;
    pthread_cond_broadcast(&work_queue.not_empty);
    pthread_mutex_unlock(&work_queue.mutex);

    // Wait until they leave
    for (int i = 0; i < threads-1; i++) {
        //fprintf(stderr, "Waiting for thread %d to exit\n", i);
        void *retval;
        pthread_join(work_queue.threads[i], &retval);
    }

    //fprintf(stderr, "All threads have quit. Destroying mutex and condition variable.\n");
    // Tidy up
    pthread_mutex_destroy(&work_queue.mutex);
    pthread_cond_destroy(&work_queue.not_empty);
    thread_pool_initialized = false;
}

WEAK void *worker(void *void_arg) {
    /*
    int id = -1;
    for (int i = 0; i < threads-1; i++) {
        if (work_queue.threads[i] == pthread_self()) id = i;
    }
    */
    worker_arg *arg = (worker_arg *)void_arg;
    //fprintf(stderr, "Worker %d: thread running\n", id);
    while (1) {
        //fprintf(stderr, "Worker %d: About to lock mutex\n", id);
        pthread_mutex_lock(&work_queue.mutex);
        //fprintf(stderr, "Worker %d: Mutex locked, checking for work\n", id);

        // we're master, and there's no more work
        if (arg && arg->job->id != arg->id) {
            // wait until other workers are done
            if (arg->job->active_workers) {
                pthread_mutex_unlock(&work_queue.mutex);
                while (true) {
                    //fprintf(stderr, "Master %d: waiting for workers to finish\n", arg->id);
                    pthread_mutex_lock(&work_queue.mutex);
                    //fprintf(stderr, "Master %d: mutex grabbed. %d workers still going\n", arg->id, arg->job->active_workers);
                    if (!arg->job->active_workers)
                        break;
                    pthread_mutex_unlock(&work_queue.mutex);
                }
            }
            // job is actually done
            pthread_mutex_unlock(&work_queue.mutex);
            //fprintf(stderr, "Master %d: This task is done.\n", arg->id);
            return NULL;
        }

        if (work_queue.shutdown) {
            pthread_mutex_unlock(&work_queue.mutex);
            //fprintf(stderr, "Worker %d: quitting\n", id);
            return NULL;
        }
            
        if (work_queue.head == work_queue.tail) {
            assert(!arg); // the master should never get here
            //fprintf(stderr, "Worker %d: Going to sleep.\n", id); fflush(stderr);
            pthread_cond_wait(&work_queue.not_empty, &work_queue.mutex);
            pthread_mutex_unlock(&work_queue.mutex);
            //fprintf(stderr, "Worker %d: Waking up.\n", id); fflush(stderr);
            continue;
        }

        //fprintf(stderr, "Worker %d: There is work\n", id);
        work *job = work_queue.jobs + work_queue.head;
        if (job->next == job->max) {
            //fprintf(stderr, "Worker %d: Found a finished job. Removing it\n", id);
            work_queue.head = (work_queue.head + 1) % MAX_JOBS;            
            job->id = 0; // mark the job done
            pthread_mutex_unlock(&work_queue.mutex);
        } else {
            // Claim some tasks
            //int claimed = (remaining + threads - 1)/threads;
            int claimed = 1;
            //fprintf(stderr, "Worker %d: Claiming %d tasks\n", id, claimed);
            work myjob = *job;
            job->next += claimed;            
            myjob.max = job->next;
            job->active_workers++;
            pthread_mutex_unlock(&work_queue.mutex);
            for (; myjob.next < myjob.max; myjob.next++) {
                //fprintf(stderr, "Worker %d: Doing job %d\n", id, myjob.next);
                myjob.f(myjob.next, myjob.closure);
                //fprintf(stderr, "Worker %d: Done with job %d\n", id, myjob.next);
            }
            pthread_mutex_lock(&work_queue.mutex);
            job->active_workers--;
            pthread_mutex_unlock(&work_queue.mutex);
        }        
    }
}

WEAK void do_par_for(void (*f)(int, uint8_t *), int min, int size, uint8_t *closure) {
    if (!thread_pool_initialized) {
        work_queue.shutdown = false;
        pthread_mutex_init(&work_queue.mutex, NULL);
        pthread_cond_init(&work_queue.not_empty, NULL);
        work_queue.head = work_queue.tail = 0;
        work_queue.ids = 1;
        char *threadStr = getenv("HL_NUMTHREADS");
        threads = 8;
        if (threadStr) {
            threads = atoi(threadStr);
        } else {
            printf("HL_NUMTHREADS not defined. Defaulting to 8 threads.\n");
        }
        if (threads > MAX_THREADS) threads = MAX_THREADS;
        for (int i = 0; i < threads-1; i++) {
            //fprintf(stderr, "Creating thread %d\n", i);
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
    
    //fprintf(stderr, "Waking up workers\n");
    // Wake up everyone
    pthread_cond_broadcast(&work_queue.not_empty);

    // Do some work myself
    //fprintf(stderr, "Doing some work on job %d\n", arg.id);
    worker((void *)(&arg));    
    //fprintf(stderr, "Parallel for done\n");
}

#ifdef _WIN32
extern "C" float roundf(float);
extern "C" double round(double);
#endif

INLINE float sqrt_f32(float x) {return sqrtf(x);}
INLINE float sin_f32(float x) {return sinf(x);}
INLINE float cos_f32(float x) {return cosf(x);}
INLINE float exp_f32(float x) {return expf(x);}
INLINE float log_f32(float x) {return logf(x);}
INLINE float pow_f32(float x, float y) {return powf(x, y);}
INLINE float floor_f32(float x) {return floorf(x);}
INLINE float ceil_f32(float x) {return ceilf(x);}
INLINE float round_f32(float x) {return roundf(x);}

INLINE double sqrt_f64(double x) {return sqrt(x);}
INLINE double sin_f64(double x) {return sin(x);}
INLINE double cos_f64(double x) {return cos(x);}
INLINE double exp_f64(double x) {return exp(x);}
INLINE double log_f64(double x) {return log(x);}
INLINE double pow_f64(double x, double y) {return pow(x, y);}
INLINE double floor_f64(double x) {return floor(x);}
INLINE double ceil_f64(double x) {return ceil(x);}
INLINE double round_f64(double x) {return round(x);}

INLINE float maxval_f32() {return FLT_MAX;}
INLINE float minval_f32() {return -FLT_MAX;}
INLINE double maxval_f64() {return DBL_MAX;}
INLINE double minval_f64() {return -DBL_MAX;}
INLINE uint8_t maxval_u8() {return 0xff;}
INLINE uint8_t minval_u8() {return 0;}
INLINE uint16_t maxval_u16() {return 0xffff;}
INLINE uint16_t minval_u16() {return 0;}
INLINE uint32_t maxval_u32() {return 0xffffffff;}
INLINE uint32_t minval_u32() {return 0;}
INLINE uint64_t maxval_u64() {return 0xffffffffffffffff;}
INLINE uint64_t minval_u64() {return 0;}
INLINE int8_t maxval_s8() {return 0x7f;}
INLINE int8_t minval_s8() {return 0x80;}
INLINE int16_t maxval_s16() {return 0x7fff;}
INLINE int16_t minval_s16() {return 0x8000;}
INLINE int32_t maxval_s32() {return 0x7fffffff;}
INLINE int32_t minval_s32() {return 0x80000000;}
INLINE int64_t maxval_s64() {return 0x7fffffffffffffff;}
INLINE int64_t minval_s64() {return 0x8000000000000000;}

INLINE int8_t abs_i8(int8_t a) {return a >= 0 ? a : -a;}
INLINE int16_t abs_i16(int16_t a) {return a >= 0 ? a : -a;}
INLINE int32_t abs_i32(int32_t a) {return a >= 0 ? a : -a;}
INLINE int64_t abs_i64(int64_t a) {return a >= 0 ? a : -a;}
INLINE float abs_f32(float a) {return a >= 0 ? a : -a;}
INLINE double abs_f64(double a) {return a >= 0 ? a : -a;}


#ifdef _WIN32
// TODO: currentTime definition for windows
#define current_time_defined
#endif

#ifndef current_time_defined
#define current_time_defined
#include <sys/time.h>

static timeval reference_clock;

WEAK int start_clock() {
    gettimeofday(&reference_clock, NULL);
    return 0;
}

WEAK int current_time() {
    timeval now;
    gettimeofday(&now, NULL);
    return
        (now.tv_sec - reference_clock.tv_sec)*1000 + 
        (now.tv_usec - reference_clock.tv_usec)/1000;
}
#endif

}
