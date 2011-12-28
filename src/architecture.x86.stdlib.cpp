#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <math.h>
#include <pthread.h>

#include "buffer.h"

extern "C" {

#define PTR_OFFSET(ptr,offset)	((void*)((char*)(ptr) + (offset)))

buffer_t* __x86_force_include_buffer_t;

void *safe_malloc(size_t x) {
    void *mem;
    x = ((x + 4095)/4096) * 4096;
    posix_memalign(&mem, 4096, x + 4096 * 3);
    //printf("Allocated %lu bytes at %p with an electric fence\n", x, mem);

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
    void (*f)(int);
    int idx;
};

void *do_work(void *arg) {
    work *job = (work *)arg;
    printf("Spawning job %d\n", job->idx);
    job->f(job->idx);
    printf("Finished job %d\n", job->idx);
    return NULL;
}

void do_par_for(void (*f)(int), int min, int size) {
    // for now, just create a thread for each loop instance
    pthread_t threads[size];
    work jobs[size];
    for (int i = 0; i < size; i++)  {
        jobs[i].f = f;
        jobs[i].idx = i;
        pthread_create(threads+i, NULL, do_work, (void *)(jobs + i));
    }
    for (int i = 0; i < size; i++) {
        pthread_join(threads[i], NULL);
    }
}

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

