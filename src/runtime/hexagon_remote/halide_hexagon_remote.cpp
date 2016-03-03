extern "C" {

#include "halide_hexagon_remote.h"
#include <sys/mman.h>
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>

#define FARF_LOW 1
#include "HAP_farf.h"

}

#include "elf.h"

#include "../HalideRuntime.h"

typedef halide_hexagon_remote_handle_t handle_t;
typedef halide_hexagon_remote_buffer buffer;


void halide_print(void *user_context, const char *str) {
    FARF(LOW, "%s", str);
}

void halide_error(void *user_context, const char *str) {
    halide_print(user_context, str);
}

void *halide_malloc(void *user_context, size_t x) {
    // Allocate enough space for aligning the pointer we return.
    const size_t alignment = 128;
    void *orig = malloc(x + alignment);
    if (orig == NULL) {
        // Will result in a failed assertion and a call to halide_error
        return NULL;
    }
    // We want to store the original pointer prior to the pointer we return.
    void *ptr = (void *)(((size_t)orig + alignment + sizeof(void*) - 1) & ~(alignment - 1));
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void halide_free(void *user_context, void *ptr) {
    free(((void**)ptr)[-1]);
}

int halide_do_task(void *user_context, halide_task_t f, int idx,
                   uint8_t *closure) {
    return f(user_context, idx, closure);
}

int halide_do_par_for(void *user_context, halide_task_t f,
                      int min, int size, uint8_t *closure) {
    for (int x = min; x < min + size; x++) {
        int result = halide_do_task(user_context, f, x, closure);
        if (result) {
            return result;
        }
    }
    return 0;
}

extern "C" {

const int map_alignment = 4096;

int halide_hexagon_remote_initialize_kernels(const unsigned char *code, int codeLen,
                                             handle_t *module_ptr) {
    int result;

    // Map some memory for the code and copy it in.
    int aligned_codeLen = (codeLen + map_alignment - 1) & ~(map_alignment - 1);
    void *executable = mmap(0, aligned_codeLen, PROT_READ | PROT_WRITE, MAP_ANON, -1, 0);
    if (executable == MAP_FAILED) {
        return -1;
    }
    memcpy(executable, code, codeLen);

    Elf::Object<uint32_t> obj;
    result = obj.init(executable);
    if (result != 0) {
        return result;
    }

    result = obj.do_relocations();
    if (result != 0) {
        return result;
    }

    handle_t base_addr = (handle_t)executable;

    // Change memory to be executable (but not writable).
    if (mprotect(executable, aligned_codeLen, PROT_READ | PROT_EXEC) < 0) {
        munmap(executable, aligned_codeLen);
        return -1;
    }

    // Initialize the runtime. The Hexagon runtime can't call any
    // system functions (because we can't link them), so we put all
    // the implementations that need to do so here, and pass poiners
    // to them in here.
    typedef int (*init_runtime_t)(halide_malloc_t user_malloc,
                                  halide_free_t custom_free,
                                  halide_print_t print,
                                  halide_error_handler_t error_handler,
                                  halide_do_par_for_t do_par_for,
                                  halide_do_task_t do_task);
    init_runtime_t init_runtime =
        (init_runtime_t)halide_hexagon_remote_get_symbol(base_addr, "halide_noos_init_runtime", 0);
    if (init_runtime == 0) {
        munmap(executable, aligned_codeLen);
        return -1;
    }
    result = init_runtime(halide_malloc,
                          halide_free,
                          halide_print,
                          halide_error,
                          halide_do_par_for,
                          halide_do_task);
    if (result != 0) {
        munmap(executable, aligned_codeLen);
        return result;
    }

    *module_ptr = base_addr;
    return 0;
}

handle_t halide_hexagon_remote_get_symbol(handle_t module_ptr, const char* name, int nameLen) {
    Elf::Object<uint32_t> obj;
    int result = obj.init((void*)module_ptr);
    if (result != 0) {
        return 0;
    }

    return (handle_t)obj.symbol_address(name);
}

int halide_hexagon_remote_run(handle_t module_ptr, handle_t function,
                              const buffer *arg_ptrs, int arg_ptrsLen,
                              buffer *outputs, int outputsLen) {
    // Get a pointer to the argv version of the pipeline.
    typedef int (*pipeline_argv_t)(void **);
    pipeline_argv_t pipeline = (pipeline_argv_t)(function);

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

int halide_hexagon_remote_release_kernels(handle_t module_ptr, int codeLen) {
    void *executable = (void *)module_ptr;
    codeLen = (codeLen + map_alignment - 1) & (map_alignment - 1);
    munmap(executable, codeLen);
    return 0;
}

}  // extern "C"
