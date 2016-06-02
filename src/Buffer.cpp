#include "Buffer.h"
#include "Debug.h"
#include "Error.h"
#include "JITModule.h"
#include "runtime/HalideRuntime.h"

namespace Halide {
namespace Internal {

namespace {
void check_buffer_size(uint64_t bytes, const std::string &name) {
    user_assert(bytes < (1UL << 31)) << "Total size of buffer " << name << " exceeds 2^31 - 1\n";
}
}

struct BufferContents {
    /** The halide_buffer_t we're wrapping. */
    halide_buffer_t buf;

    /** Storage for the dimensions */
    std::vector<halide_dimension_t> shape;

    /** If we made the allocation ourselves via a Buffer constructor,
     * and hence should delete it when this buffer dies, then this
     * pointer is set to the memory we need to free. Otherwise it's
     * nullptr. */
    uint8_t *allocation;

    /** How many Buffer objects point to this BufferContents */
    mutable RefCount ref_count;

    /** What is the name of the buffer? Useful for debugging symbols. */
    std::string name;

    BufferContents(Type t, const std::vector<int> &sizes, uint8_t *data, const std::string &n) :
        buf {0},
        allocation(nullptr),
        name(n.empty() ? unique_name('b') : n) {

        user_assert(t.lanes() == 1) << "Can't create of a buffer of a vector type";

        buf.type = (halide_type_t)t;
        buf.dimensions = sizes.size();
        shape.resize(buf.dimensions);
        buf.dim = &shape[0];

        uint64_t size = t.bytes();
        for (int s : sizes) {
            size *= s;
            check_buffer_size(size, name);
        }

        if (!data) {
            size += 32;
            check_buffer_size(size, name);
            allocation = (uint8_t *)calloc(1, (size_t)size);
            user_assert(allocation) << "Out of memory allocating buffer " << name << " of size " << size << "\n";
            buf.host = (uint8_t *)(((intptr_t)allocation + 31) & (~31));
        } else {
            buf.host = data;
        }
        for (int i = 0; i < buf.dimensions; i++) {
            buf.dim[i].extent = sizes[i];
            if (i == 0) {
                buf.dim[i].stride = 1;
            } else {
                buf.dim[i].stride = buf.dim[i-1].stride * buf.dim[i-1].extent;
            }
        }
    }


    BufferContents(const halide_buffer_t *b, const std::string &n) :
        buf(*b),
        allocation(nullptr),
        name(n.empty() ? unique_name('b') : n) {
        if (b->dim) {
            shape = std::vector<halide_dimension_t>(b->dim, b->dim + b->dimensions);
            buf.dim = &shape[0];
        }
    }
};

template<>
EXPORT RefCount &ref_count<BufferContents>(const BufferContents *p) {
    return p->ref_count;
}

template<>
EXPORT void destroy<BufferContents>(const BufferContents *p) {
    int error = halide_device_free(nullptr, const_cast<halide_buffer_t *>(&p->buf));
    user_assert(!error) << "Failed to free device buffer\n";
    free(p->allocation);

    delete p;
}

}

namespace {
std::string make_buffer_name(const std::string &n, Buffer *b) {
    if (n.empty()) {
        return Internal::make_entity_name(b, "Halide::Buffer", 'b');
    } else {
        return n;
    }
}
}

Buffer::Buffer(Type t, const std::vector<int32_t> &sizes,
               uint8_t* data, const std::string &name) :
    contents(new Internal::BufferContents(t, sizes, data,
                                          make_buffer_name(name, this))) {
}

Buffer::Buffer(const halide_buffer_t *buf, const std::string &name) :
    contents(new Internal::BufferContents(buf, make_buffer_name(name, this))) {
}

void *Buffer::host_ptr() const {
    user_assert(defined()) << "Buffer is undefined\n";
    return (void *)contents->buf.host;
}

halide_buffer_t *Buffer::raw_buffer() const {
    user_assert(defined()) << "Buffer is undefined\n";
    return &(contents->buf);
}

uint64_t Buffer::device_handle() const {
    user_assert(defined()) << "Buffer is undefined\n";
    return contents->buf.device;
}

bool Buffer::host_dirty() const {
    user_assert(defined()) << "Buffer is undefined\n";
    return contents->buf.host_dirty();
}

void Buffer::set_host_dirty(bool dirty) {
    user_assert(defined()) << "Buffer is undefined\n";
    contents->buf.set_host_dirty(dirty);
}

bool Buffer::device_dirty() const {
    user_assert(defined()) << "Buffer is undefined\n";
    return contents->buf.device_dirty();
}

void Buffer::set_device_dirty(bool dirty) {
    user_assert(defined()) << "Buffer is undefined\n";
    contents->buf.set_device_dirty(dirty);
}

int Buffer::dimensions() const {
    user_assert(defined()) << "Buffer is undefined\n";
    return contents->buf.dimensions;
}

Buffer::Dimension Buffer::dim(int d) const {
    user_assert(defined()) << "Buffer is undefined\n";
    user_assert(d >= 0 && d < dimensions())
        << "Asking for dimension " << d
        << " from a buffer with " << dimensions() << " dimensions\n";
    return contents->buf.dim[d];
}

void Buffer::set_min(const std::vector<int> &m) {
    user_assert(defined()) << "Buffer is undefined\n";
    user_assert((int)m.size() == dimensions())
        << "Wrong number of elements (" << m.size()
        << ") for dimensionality of buffer (" << dimensions() << ")\n";
    for (size_t i = 0; i < m.size(); i++) {
        contents->buf.dim[i].min = m[i];
    }
}

Type Buffer::type() const {
    user_assert(defined()) << "Buffer is undefined\n";
    return contents->buf.type;
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

int Buffer::free_device_buffer() {
    return halide_device_free(nullptr, raw_buffer());
}


}
