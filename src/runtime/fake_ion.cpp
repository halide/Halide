#include "mini_ion.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Ion {

// Allocate an ION buffer, and map it, returning the mapped pointer.
WEAK void *ion_alloc(void *user_context, size_t len, int heap_id, int *out_fd) {
    void *ret = halide_malloc(user_context, len);
    if (ret && out_fd) {
        *out_fd = -1;
    }

    return ret;
}

// Free a previously allocated ION buffer.
WEAK void ion_free(void *user_context, void *ion) {
    halide_free(user_context, ion);
}

}}}}  // namespace Halide::Runtime::Internal::Ion
