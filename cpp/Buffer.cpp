#include "buffer.h"
#include "Buffer.h"

namespace Halide {

namespace Internal {

struct BufferContents {
    buffer_t buf;
    int ref_count;
    Type type;
    bool should_free;
}

template<>
int &ref_count<BufferContents>(const BufferContents *p) {
    return p->ref_count;
}

template<>
void destroy<BufferContents>(const BufferContents *p) {
    if (!p) return;
    if (should_free) free(p->buf.host);
    delete p;

}

}

using namespace Internal;

Buffer::Buffer() : IntrusivePtr<BufferContents>(NULL) {    
}

void *Buffer::host_ptr() const {
    assert(defined());
    return (void *)ptr->buf.host_ptr;
}

const buffer_t *Buffer::raw_buffer() const {
    assert(defined());
    return &(ptr->buf);
}

uint64_t Buffer::device_handle() const {
    assert(defined());
    return ptr->buf.dev;
}

bool Buffer::host_dirty() const {
    assert(defined());
    return ptr->buf.host_dirty;
}

bool Buffer::device_dirty() const {
    assert(defined());
    return ptr->buf.device_dirty;
}

int Buffer::extent(int dim) const {
    assert(defined());
    assert(dim >= 0 && dim < 4 && "We only support 4-dimensional buffers for now");
    return ptr->buf.extent[dim];
}

int Buffer::stride(int dim) const {
    assert(defined());
    assert(dim >= 0 && dim < 4 && "We only support 4-dimensional buffers for now");
    return ptr->buf.stride[dim];
}

int Buffer::min(int dim) const {
    assert(defined());
    assert(dim >= 0 && dim < 4 && "We only support 4-dimensional buffers for now");
    return ptr->buf.min[dim];
}

Type Buffer::type() const {
    assert(defined());
    return ptr->type;
}
