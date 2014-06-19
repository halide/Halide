extern "C" {

static size_t __buf_size(void *user_context, buffer_t *buf) {
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

// A host <-> dev copy is expressed as a 4D array of copy tasks to perform
// each task starts at a different offset and copies some contiguous
// number of bytes. This struct is quite similar to a buffer_t.
#define MAX_COPY_DIMS 4
struct __dev_copy {
    uint64_t src, dst;
    // How many contiguous bytes to copy per task
    uint64_t chunk_size;
    // There is some multidimensional array of subtasks.
    uint64_t extent[MAX_COPY_DIMS];
    // Strides (in bytes) for each dimension.
    uint64_t stride[MAX_COPY_DIMS];
};

// Rewrite a __dev_copy to do fewer, larger contiguous copies if possible.
static void __simplify_dev_copy(__dev_copy *c) {
    while (1) {
        // Look for a stride that matches the chunk_size
        bool did_something = false;
        for (int i = 0; i < MAX_COPY_DIMS; i++) {
            if (c->stride[i] && c->stride[i] == c->chunk_size) {
                did_something = true;
                // Remove that dimension and make the chunk_size larger instead.
                c->chunk_size *= c->extent[i];
                for (int j = i+1; j < MAX_COPY_DIMS; j++) {
                    c->extent[j-1] = c->extent[j];
                    c->stride[j-1] = c->stride[j];
                }
                c->extent[MAX_COPY_DIMS-1] = 1;
                c->stride[MAX_COPY_DIMS-1] = 0;
            }
        }
        if (!did_something) {
            return;
        }
    }
}

static __dev_copy __make_host_to_dev_copy(const buffer_t *buf) {
    __dev_copy c;
    c.src = (uint64_t)buf->host;
    c.dst = buf->dev;
    c.chunk_size = buf->elem_size;
    // By default, dimensions have stride 0 and extent 1
    for (int i = 0; i < MAX_COPY_DIMS; i++) {
        c.extent[i] = 1;
        c.stride[i] = 0;
    }
    // Copy over the well-defined dims from the buffer_t
    for (int i = 0; i < 4 && buf->extent[i]; i++) {
        c.extent[i] = buf->extent[i];
        c.stride[i] = buf->stride[i] * buf->elem_size;
    };
    __simplify_dev_copy(&c);
    return c;
}

static __dev_copy __make_dev_to_host_copy(const buffer_t *buf) {
    // Just make a host to dev copy and swap src and dst
    __dev_copy c = __make_host_to_dev_copy(buf);
    uint64_t tmp = c.src;
    c.src = c.dst;
    c.dst = tmp;
    return c;
}

}
