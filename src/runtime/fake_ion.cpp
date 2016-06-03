#include "mini_ion.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Ion {

struct allocation_record {
    void *original;
};

// Allocate an ION buffer, and map it, returning the mapped pointer.
WEAK void *ion_alloc(void *user_context, size_t len, int heap_id, int *out_fd) {
    const size_t align = 128;

    // Align the allocation size.
    len = (len + align - 1) & ~(align - 1);

    // Allocate enough space to hold information about the allocation prior to the pointer we return.
    len += align;

    void *original = halide_malloc(user_context, len);

    // Store the original ptr before the pointer we return.
    allocation_record rec = { original };
    void *ret = reinterpret_cast<char *>(original) + align;
    halide_assert(user_context, sizeof(allocation_record) <= align);
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
