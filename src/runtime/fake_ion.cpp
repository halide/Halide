#include "mini_ion.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Ion {

struct allocation_record {
    void *original;
};

// Allocate an ION buffer, and map it, returning the mapped pointer.
WEAK void *ion_alloc(void *user_context, size_t len, int heap_id, int *out_fd) {
    const size_t align = 128;

    void *original = halide_malloc(user_context, len + align + sizeof(allocation_record));

    // Store the original ptr before the pointer we return.
    void *ret = reinterpret_cast<void *>(((reinterpret_cast<uintptr_t>(original) + align) / align) * align);

    allocation_record rec = { original };
    memcpy(reinterpret_cast<allocation_record *>(ret) - 1, &rec, sizeof(rec));

    if (ret && out_fd) {
        *out_fd = -1;
    }
    return ret;
}

// Free a previously allocated ION buffer.
WEAK void ion_free(void *user_context, void *ion) {
    if (!ion) return;
    allocation_record rec = *(reinterpret_cast<allocation_record *>(ion) - 1);
    halide_free(user_context, rec.original);
}

}}}}  // namespace Halide::Runtime::Internal::Ion
