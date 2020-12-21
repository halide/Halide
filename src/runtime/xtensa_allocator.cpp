#ifdef __cplusplus
extern "C" {
#endif

typedef __SIZE_TYPE__ size_t;

extern void *malloc(size_t);
extern void free(void *);

// NOTE(vksnk): original definition has WEAK in front of it, but xtensa linker
// doesn't seem to handle it correctly.
int halide_malloc_alignment();

void *halide_malloc(void *user_context, size_t x) {
    // Allocate enough space for aligning the pointer we return.
    const size_t alignment = halide_malloc_alignment();
    void *orig = malloc(x + alignment);
    if (orig == 0) {
        // Will result in a failed assertion and a call to halide_error
        return 0;
    }
    // We want to store the original pointer prior to the pointer we return.
    void *ptr = (void *)(((size_t)orig + alignment + sizeof(void *) - 1) &
                         ~(alignment - 1));
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void halide_free(void *user_context, void *ptr) {
    free(((void **)ptr)[-1]);
}

#ifdef __cplusplus
}  // extern "C"
#endif
