#include <rpcmem.h>

extern "C" {

void halide_hexagon_host_malloc_init() {
    rpcmem_init();
}

void halide_hexagon_host_malloc_deinit() {
    rpcmem_deinit();
}

void *halide_hexagon_host_malloc(size_t size) {
    // This heap is much faster than RPCMEM_DEFAULT_HEAP.
    const int system_heap = 25;
    return rpcmem_alloc(system_heap, RPCMEM_DEFAULT_FLAGS, size);
}

void halide_hexagon_host_free(void *ptr) {
    rpcmem_free(ptr);
}

}
