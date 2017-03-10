// This file contains a compatibility layer to implement old RPC calls
// in terms of new ones.

#include "bin/src/halide_hexagon_remote.h"

typedef halide_hexagon_remote_handle_t handle_t;

extern "C" {

handle_t halide_hexagon_remote_get_symbol(handle_t module_ptr,
                                          const char* name, int nameLen) {
    handle_t sym = 0;
    int result = halide_hexagon_remote_get_symbol_v3(module_ptr, name, nameLen, false, &sym);
    return result == 0 ? sym : 0;
}

int halide_hexagon_remote_initialize_kernels(const unsigned char *code, int codeLen,
                                             handle_t *module_ptr) {
   return halide_hexagon_remote_initialize_kernels_v2(code, codeLen, false, module_ptr);
}

}  // extern "C"
