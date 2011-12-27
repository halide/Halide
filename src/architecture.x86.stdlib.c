#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <math.h>

void *safe_malloc(size_t x) {
    void *mem;
    x = ((x + 4095)/4096) * 4096;
    posix_memalign(&mem, 4096, x + 4096 * 3);
    //printf("Allocated %lu bytes at %p with an electric fence\n", x, mem);

    // write the end address to unprotect in the initial fence
    ((void **)mem)[0] = mem + x + 4096;
    
    mprotect(mem, 4096, PROT_NONE);
    mprotect(mem + x + 4096, 4096, PROT_NONE);
    
    return mem + 4096;
}

void safe_free(void *ptr) {
    void *start = ptr - 4096;
    mprotect(start, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
    void *end = ((void **)start)[0];
    mprotect(end, 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
    free(start);
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
