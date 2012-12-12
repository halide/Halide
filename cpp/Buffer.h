#ifndef HALIDE_BUFFER_H
#define HALIDE_BUFFER_H

#include "IntrusivePtr.h"
#include "Type.h"
#include "stdint.h"

struct buffer_t;

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
class BufferContents;
}

class Buffer : public Internal::IntrusivePtr<Internal::BufferContents> {
public:
    Buffer();

    const buffer_t *raw_buffer() const;
    void *host_ptr() const;
    uint64_t device_handle() const;
    bool host_dirty() const;
    bool device_dirty() const;
    int extent(int) const;
    int stride(int) const;
    int min(int) const;
    Type type() const;   
};


}

#endif
