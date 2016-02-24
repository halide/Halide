#include "halide_hexagon_remote.h"

#include <sys/mman.h>
#include <memory.h>

extern "C" {

static const int alignment = 4096;

int halide_hexagon_remote_initialize_kernels(const unsigned char *code, int codeLen, halide_hexagon_remote_uintptr_t *module_ptr) {
    // Map some memory for the code and copy it in.
    int aligned_codeLen = (codeLen + alignment - 1) & (alignment - 1);
    void *exec = mmap(0, aligned_codeLen, PROT_READ | PROT_WRITE, MAP_ANON, -1, 0);
    if (exec == MAP_FAILED) {
        return -1;
    }
    memcpy(exec, code, codeLen);

    // Change memory to be executable (but not writable).
    if (mprotect(exec, aligned_codeLen, PROT_READ | PROT_EXEC) < 0) {
        munmap(exec, aligned_codeLen);
        return -1;
    }

    *module_ptr = (halide_hexagon_remote_uintptr_t)exec;
    return 0;
}

typedef int (*pipeline_argv)(void **);

int halide_hexagon_remote_run(halide_hexagon_remote_uintptr_t module_ptr, int offset,
                              const halide_hexagon_remote_buffer *arg_ptrs, int arg_ptrsLen,
                              halide_hexagon_remote_buffer *outputs, int outputsLen) {
    // Get a pointer to the argv version of the pipeline.
    pipeline_argv pipeline = (pipeline_argv)(module_ptr + offset);

    // Construct a list of arguments.
    void **args = (void **)__builtin_alloca((arg_ptrsLen + outputsLen) * sizeof(void *));
    for (int i = 0; i < arg_ptrsLen; i++) {
        args[i] = arg_ptrs[i].data;
    }
    for (int i = 0; i < outputsLen; i++) {
        args[i + arg_ptrsLen] = outputs[i].data;
    }

    // Call the pipeline and return the result.
    return pipeline(args);
}

int halide_hexagon_remote_release_kernels(halide_hexagon_remote_uintptr_t module_ptr, int codeLen) {
    void *exec = (void *)module_ptr;
    codeLen = (codeLen + alignment - 1) & (alignment - 1);
    munmap(exec, codeLen);
    return 0;
}

}  // extern "C"
