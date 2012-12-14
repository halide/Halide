#ifndef HALIDE_BUFFER_H
#define HALIDE_BUFFER_H

#include "IntrusivePtr.h"
#include "Type.h"
#include "stdint.h"
#include "buffer.h"
#include "assert.h"

namespace Halide {

/* This class represents a halide buffer that can be passed to a
 * halide function. It may be stored in main memory, or some other
 * memory space (e.g. a gpu). If you want to use this as an Image, see
 * the Image class. Casting a Buffer to an Image will do any
 * appropriate copy-back. This class is a fairly thin wrapper on a
 * buffer_t, which is the C-style type Halide uses for passing buffers
 * around. 
 */

namespace Internal {
struct BufferContents {
    buffer_t buf;
    mutable int ref_count;
    Type type;
    bool own_host_allocation;
};
}

class Buffer {
private:
    Internal::IntrusivePtr<const Internal::BufferContents> contents;
public:
    Buffer() : contents(NULL) {}

    void *host_ptr() const {
        assert(defined());
        return (void *)contents.ptr->buf.host;
    }
    
    const buffer_t *raw_buffer() const {
        assert(defined());
        return &(contents.ptr->buf);
    }
    
    uint64_t device_handle() const {
        assert(defined());
        return contents.ptr->buf.dev;
    }
    
    bool host_dirty() const {
        assert(defined());
        return contents.ptr->buf.host_dirty;
    }
    
    bool device_dirty() const {
        assert(defined());
        return contents.ptr->buf.dev_dirty;
    }
    
    int extent(int dim) const {
        assert(defined());
        assert(dim >= 0 && dim < 4 && "We only support 4-dimensional buffers for now");
        return contents.ptr->buf.extent[dim];
    }
    
    int stride(int dim) const {
        assert(defined());
        assert(dim >= 0 && dim < 4 && "We only support 4-dimensional buffers for now");
        return contents.ptr->buf.stride[dim];
    }
    
    int min(int dim) const {
        assert(defined());
        assert(dim >= 0 && dim < 4 && "We only support 4-dimensional buffers for now");
        return contents.ptr->buf.min[dim];
    }
    
    Type type() const {
        assert(defined());
        return contents.ptr->type;
    }    

    bool same_as(const Buffer &other) const {
        return contents.same_as(other.contents);
    }

    bool defined() const {
        return contents.defined();
    }

    // Any Image<T> class is allowed to mess with my internals
    template<typename T>
    friend class Image;
};

namespace Internal {
template<>
inline int &ref_count<BufferContents>(const BufferContents *p) {
    return p->ref_count;
}

template<>
inline void destroy<BufferContents>(const BufferContents *p) {
    if (p->own_host_allocation) free(p->buf.host);
    delete p;
}
}
}

#endif
