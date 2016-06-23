#include "Buffer.h"
#include "Debug.h"
#include "Error.h"
#include "JITModule.h"
#include "runtime/HalideRuntime.h"
#include "Target.h"

namespace Halide {
namespace Internal {

namespace {

uint64_t multiply_buffer_size_check_overflow(uint64_t size, uint64_t factor, const std::string &name) {
    // Ignore the dimensions for which the extent is zero.
    if (!factor) return size;

    // Multiply and check for 64-bit overflow
    uint64_t result = size * factor;
    bool overflow = (result / factor) != size;

    // Check against the limits Halide internally assumes in its compiled code.
    overflow |= (sizeof(size_t) == 4) && ((result >> 31) != 0);

    // In 64-bit with LargeBuffers *not* set, the limit above is the
    // correct one, however at Buffer creation time we don't know what
    // pipelines it will be used in, so we must be conservative and
    // defer the error until the user actually passes the buffer into
    // a pipeline they shouldn't have.
    overflow |= (sizeof(size_t) == 8) && ((result >> 63) != 0);

    // Assert there was no overflow.
    user_assert(!overflow)
        << "Total size of buffer " << name << " exceeds 2^" << ((sizeof(size_t) * 8) - 1) << " - 1\n";
    return result;
}

}

struct BufferContents {
    /** The buffer_t object we're wrapping. */
    buffer_t buf;

    /** The type of the allocation. buffer_t's don't currently track this so we do it here. */
    Type type;

    /** If we made the allocation ourselves via a Buffer constructor,
     * and hence should delete it when this buffer dies, then this
     * pointer is set to the memory we need to free. Otherwise it's
     * nullptr. */
    uint8_t *allocation;

    /** How many Buffer objects point to this BufferContents */
    mutable RefCount ref_count;

    /** What is the name of the buffer? Useful for debugging symbols. */
    std::string name;

    BufferContents(Type t, int x_size, int y_size, int z_size, int w_size,
                   uint8_t* data, const std::string &n) :
        type(t), allocation(nullptr), name(n.empty() ? unique_name('b') : n) {
        user_assert(t.lanes() == 1) << "Can't create of a buffer of a vector type";
        buf.elem_size = t.bytes();
        uint64_t size = 1;
        size = multiply_buffer_size_check_overflow(size, x_size, name);
        size = multiply_buffer_size_check_overflow(size, y_size, name);
        size = multiply_buffer_size_check_overflow(size, z_size, name);
        size = multiply_buffer_size_check_overflow(size, w_size, name);
        size = multiply_buffer_size_check_overflow(size, buf.elem_size, name);

        if (!data) {
            // There's no way for this to overflow without the buffer already being > 2^63-1
            size += 32;
            allocation = (uint8_t *)calloc(1, (size_t)size);
            user_assert(allocation) << "Out of memory allocating buffer " << name << " of size " << size << "\n";
            buf.host = allocation;
            while ((size_t)(buf.host) & 0x1f) buf.host++;
        } else {
            buf.host = data;
        }
        buf.dev = 0;
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

    BufferContents(Type t, const buffer_t *b, const std::string &n) :
        type(t), allocation(nullptr), name(n.empty() ? unique_name('b') : n) {
        buf = *b;
        user_assert(t.lanes() == 1) << "Can't create of a buffer of a vector type";
    }
};

template<>
EXPORT RefCount &ref_count<BufferContents>(const BufferContents *p) {
    return p->ref_count;
}

template<>
EXPORT void destroy<BufferContents>(const BufferContents *p) {
    // Ignore errors. We may be cleaning up a buffer after an earlier
    // error, and asserting would re-raise it.
    halide_device_free(nullptr, const_cast<buffer_t *>(&p->buf));
    free(p->allocation);
    delete p;
}

}

namespace {
int32_t size_or_zero(const std::vector<int32_t> &sizes, size_t index) {
    return (index < sizes.size()) ? sizes[index] : 0;
}

std::string make_buffer_name(const std::string &n, Buffer *b) {
    if (n.empty()) {
        return Internal::make_entity_name(b, "Halide::Buffer", 'b');
    } else {
        return n;
    }
}
}

Buffer::Buffer(Type t, int x_size, int y_size, int z_size, int w_size,
               uint8_t* data, const std::string &name) :
    contents(new Internal::BufferContents(t, x_size, y_size, z_size, w_size, data,
                                          make_buffer_name(name, this))) {
}

Buffer::Buffer(Type t, const std::vector<int32_t> &sizes,
               uint8_t* data, const std::string &name) :
    contents(new Internal::BufferContents(t,
                                          size_or_zero(sizes, 0),
                                          size_or_zero(sizes, 1),
                                          size_or_zero(sizes, 2),
                                          size_or_zero(sizes, 3),
                                          data,
                                          make_buffer_name(name, this))) {
    user_assert(sizes.size() <= 4) << "Buffer dimensions greater than 4 are not supported.";
}

Buffer::Buffer(Type t, const buffer_t *buf, const std::string &name) :
    contents(new Internal::BufferContents(t, buf,
                                          make_buffer_name(name, this))) {
}

void *Buffer::host_ptr() const {
    user_assert(defined()) << "Buffer is undefined\n";
    return (void *)contents->buf.host;
}

buffer_t *Buffer::raw_buffer() const {
    user_assert(defined()) << "Buffer is undefined\n";
    return &(contents->buf);
}

uint64_t Buffer::device_handle() const {
    user_assert(defined()) << "Buffer is undefined\n";
    return contents->buf.dev;
}

bool Buffer::host_dirty() const {
    user_assert(defined()) << "Buffer is undefined\n";
    return contents->buf.host_dirty;
}

void Buffer::set_host_dirty(bool dirty) {
    user_assert(defined()) << "Buffer is undefined\n";
    contents->buf.host_dirty = dirty;
}

bool Buffer::device_dirty() const {
    user_assert(defined()) << "Buffer is undefined\n";
    return contents->buf.dev_dirty;
}

void Buffer::set_device_dirty(bool dirty) {
    user_assert(defined()) << "Buffer is undefined\n";
    contents->buf.dev_dirty = dirty;
}

int Buffer::dimensions() const {
    for (int i = 0; i < 4; i++) {
        if (extent(i) == 0) return i;
    }
    return 4;
}

int Buffer::extent(int dim) const {
    user_assert(defined()) << "Buffer is undefined\n";
    user_assert(dim >= 0 && dim < 4) << "We only support 4-dimensional buffers for now";
    return contents->buf.extent[dim];
}

int Buffer::stride(int dim) const {
    user_assert(defined());
    user_assert(dim >= 0 && dim < 4) << "We only support 4-dimensional buffers for now";
    return contents->buf.stride[dim];
}

int Buffer::min(int dim) const {
    user_assert(defined()) << "Buffer is undefined\n";
    user_assert(dim >= 0 && dim < 4) << "We only support 4-dimensional buffers for now";
    return contents->buf.min[dim];
}

void Buffer::set_min(int m0, int m1, int m2, int m3) {
    user_assert(defined()) << "Buffer is undefined\n";
    contents->buf.min[0] = m0;
    contents->buf.min[1] = m1;
    contents->buf.min[2] = m2;
    contents->buf.min[3] = m3;
}

Type Buffer::type() const {
    user_assert(defined()) << "Buffer is undefined\n";
    return contents->type;
}

bool Buffer::same_as(const Buffer &other) const {
    return contents.same_as(other.contents);
}

bool Buffer::defined() const {
    return contents.defined();
}

const std::string &Buffer::name() const {
    return contents->name;
}

Buffer::operator Argument() const {
    return Argument(name(), Argument::InputBuffer, type(), dimensions());
}

int Buffer::copy_to_host() {
    return halide_copy_to_host(nullptr, raw_buffer());
}

int Buffer::device_sync() {
    return halide_device_sync(nullptr, raw_buffer());
}

int Buffer::copy_to_device() {
  return halide_copy_to_device(nullptr, raw_buffer(), nullptr);
}

int Buffer::free_dev_buffer() {
    return halide_device_free(nullptr, raw_buffer());
}


}
