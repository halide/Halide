#include "runtime_internal.h"

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK uint64_t halide_allocation_cache_size = 0;
WEAK uint64_t halide_allocation_cache_used = 0;

}}}

extern "C" {

WEAK uint64_t halide_allocation_cache_increase_used(void *user_context, uint64_t amount) {
    return __sync_fetch_and_add(&halide_allocation_cache_used, amount);
}

WEAK uint64_t halide_allocation_cache_decrease_used(void *user_context, uint64_t amount) {
    return __sync_fetch_and_sub(&halide_allocation_cache_used, amount);
}

WEAK void halide_allocation_cache_set_size(uint64_t size) {
    halide_allocation_cache_size = size;
}

WEAK uint64_t halide_allocation_cache_get_size(void *user_context) {
    return halide_allocation_cache_size;
}

}
