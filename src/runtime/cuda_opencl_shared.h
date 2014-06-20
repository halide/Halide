extern "C" {

// Compute the total amount of memory we'd need to allocate on gpu to
// represent a given buffer (using the same strides as the host
// allocation).
static size_t _buf_size(void *user_context, buffer_t *buf) {
    size_t size = 0;
    for (size_t i = 0; i < sizeof(buf->stride) / sizeof(buf->stride[0]); i++) {
        size_t total_dim_size = buf->elem_size * buf->extent[i] * buf->stride[i];
        if (total_dim_size > size) {
            size = total_dim_size;
        }
    }
    halide_assert(user_context, size);
    return size;
}

// A host <-> dev copy should be done with the fewest possible number
// of contiguous copies to minimize driver overhead. If our buffer_t
// has strides larger than its extents (e.g. because it represents a
// sub-region of a larger buffer_t) we can't safely copy it back and
// forth using a single contiguous copy, because we'd clobber
// in-between values that another thread might be using.  In the best
// case we can do a single contiguous copy, but in the worst case we
// need to individually copy over every pixel.
//
// This problem is made extra difficult by the fact that the ordering
// of the dimensions in a buffer_t doesn't relate to memory layout at
// all, so the strides could be in any order.
//
// We solve it by representing a copy job we need to perform as a
// _dev_copy struct. It describes a 4D array of copies to
// perform. Initially it describes copying over a single pixel at a
// time. We then try to discover contiguous groups of copies that can
// be coalesced into a single larger copy.

// The struct that describes a host <-> dev copy to perform.
#define MAX_COPY_DIMS 4
struct _dev_copy {
    uint64_t src, dst;
    // The multidimensional array of contiguous copy tasks that need to be done.
    uint64_t extent[MAX_COPY_DIMS];
    // The strides (in bytes) that separate adjacent copy tasks in each dimension.
    uint64_t stride_bytes[MAX_COPY_DIMS];
    // How many contiguous bytes to copy per task
    uint64_t chunk_size;
};

static _dev_copy _make_host_to_dev_copy(const buffer_t *buf) {
    // Make a copy job representing copying the first pixel only.
    _dev_copy c;
    c.src = (uint64_t)buf->host;
    c.dst = buf->dev;
    c.chunk_size = buf->elem_size;
    for (int i = 0; i < MAX_COPY_DIMS; i++) {
        c.extent[i] = 1;
        c.stride_bytes[i] = 0;
    }

    // Now expand it to copy all the pixels (one at a time) by taking
    // the extents and strides from the buffer_t.
    for (int i = 0; i < 4 && buf->extent[i]; i++) {
        c.extent[i] = buf->extent[i];
        c.stride_bytes[i] = buf->stride[i] * buf->elem_size;
    };

    // Now attempt to coalesce the many small copies into fewer larger copies.
    while (1) {
        // Look for a stride that matches the current chunk_size. This
        // means that the copy tasks along that dimension are abutting
        // in memory, so that whole dimension can actually be folded
        // into a single larger copy.
        bool did_something = false;
        for (int i = 0; i < MAX_COPY_DIMS; i++) {
            if (c.stride_bytes[i] && c.stride_bytes[i] == c.chunk_size) {
                did_something = true;

                // Fold that dimension's extent into the chunk_size.
                c.chunk_size *= c.extent[i];

                // Erase the dimension from the list of dimensions to iterate over.
                for (int j = i+1; j < MAX_COPY_DIMS; j++) {
                    c.extent[j-1] = c.extent[j];
                    c.stride_bytes[j-1] = c.stride_bytes[j];
                }
                c.extent[MAX_COPY_DIMS-1] = 1;
                c.stride_bytes[MAX_COPY_DIMS-1] = 0;
            }
        }
        if (!did_something) {
            // No further way to combine copy tasks was found, so we're done.
            return c;
        }
    }

    // Unreachable.
    return c;
}

static _dev_copy _make_dev_to_host_copy(const buffer_t *buf) {
    // Just make a host to dev copy and swap src and dst
    _dev_copy c = _make_host_to_dev_copy(buf);
    uint64_t tmp = c.src;
    c.src = c.dst;
    c.dst = tmp;
    return c;
}

}
