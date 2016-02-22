#include "halide_remote.h"

#include <cstdint>

extern "C" {

int halide_remote_initialize_kernels(const unsigned char* code, int codeLen, halide_remote_uintptr_t* module_ptr) {
    *module_ptr = 0;
    return 0;
}

int halide_remote_run(halide_remote_uintptr_t module_ptr, int offset,
                      const halide_remote_buffer* arg_ptrs, int arg_ptrsLen,
                      const int* arg_sizes, int arg_sizesLen,
                      halide_remote_buffer* outputs, int outputsLen) {
    return 0;
}

int halide_remote_release_kernels(halide_remote_uintptr_t module_ptr) {
    return 0;
}

}  // extern "C"
