#ifndef HALIDE_BUFFER_H
#define HALIDE_BUFFER_H

#include "IntrusivePtr.h"
#include "Type.h"
#include "Argument.h"
#include "Util.h"
#include "buffer_t.h"
#include <assert.h>
#include <stdint.h>

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
    std::string name;

    BufferContents(Type t, int x_size, int y_size, int z_size, int w_size) : 
        type(t), own_host_allocation(true), name(unique_name('b')) {
        assert(t.width == 1 && "Can't create of a buffer of a vector type");
        buf.elem_size = t.bits / 8;
        buf.host = (uint8_t *)calloc(buf.elem_size, x_size*y_size*z_size*w_size);
        buf.host_dirty = false;
        buf.dev_dirty = false;
        buf.extent[0] = x_size;
        buf.extent[1] = y_size;
        buf.extent[2] = z_size;
        buf.extent[3] = w_size;
        buf.stride[0] = 1;
        buf.stride[1] = x_size;
        buf.stride[2] = x_size*y_size;
        buf.stride[3] = x_size*y_size*z_size;
        buf.min[0] = 0;
        buf.min[1] = 0;
        buf.min[2] = 0;
        buf.min[3] = 0;
    }

    BufferContents(Type t, const buffer_t *b) :
        type(t), own_host_allocation(false) {
        buf = *b;
        assert(t.width == 1 && "Can't create of a buffer of a vector type");
    }
};
}

class Buffer {
private:
    Internal::IntrusivePtr<const Internal::BufferContents> contents;
public:
    Buffer() : contents(NULL) {}

    Buffer(Type t, int x_size, int y_size = 1, int z_size = 1, int w_size = 1) : 
        contents(new Internal::BufferContents(t, x_size, y_size, z_size, w_size)) {
    }
    
    Buffer(Type t, const buffer_t *buf) : 
        contents(new Internal::BufferContents(t, buf)) {
    }

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

    const std::string &name() const {
        return contents.ptr->name;
    }

    operator Argument() const {
        return Argument(name(), true, type());
    }

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
