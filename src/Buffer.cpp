#include "Buffer.h"
#include "Debug.h"
#include "Error.h"
#include "JITModule.h"
#include "runtime/HalideRuntime.h"
#include "Target.h"
#include "Var.h"
#include "IREquality.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

struct BufferContents {
    Image<void> image;
    std::string name;
    mutable RefCount ref_count;
};

template<>
EXPORT RefCount &ref_count<BufferContents>(const BufferContents *p) {
    return p->ref_count;
}

template<>
EXPORT void destroy<BufferContents>(const BufferContents *p) {
    delete p;
}

namespace {
std::string make_buffer_name(const std::string &n, Buffer *b) {
    if (n.empty()) {
        return Internal::make_entity_name(b, "Halide::Internal::Buffer", 'b');
    } else {
        return n;
    }
}
}

Buffer::Buffer(const Image<void> &buf, std::string name) :
    contents(new Internal::BufferContents {Image<void>(buf), make_buffer_name(name, this)}) {}

Buffer::Buffer(Type t, const buffer_t &buf, std::string name) :
    contents(new Internal::BufferContents {Image<void>(t, buf), make_buffer_name(name, this)}) {}

Buffer::Buffer(Type t, const std::vector<int> &size, std::string name) :
    contents(new Internal::BufferContents {Image<void>(t, size), make_buffer_name(name, this)}) {}

bool Buffer::same_as(const Buffer &other) const {
    return contents.same_as(other.contents);
}

Image<void> &Buffer::get() {
    return contents->image;
}

const Image<void> &Buffer::get() const {
    return contents->image;
}

bool Buffer::defined() const {
    return contents->image;
}

const std::string &Buffer::name() const {
    return contents->name;
}

Buffer::operator Argument() const {
    return Argument(name(), Argument::InputBuffer, type(), dimensions());
}

Type Buffer::type() const {
    return contents->image.type();
}

int Buffer::dimensions() const {
    return contents->image.dimensions();
}

Image<void>::Dimension Buffer::dim(int i) const {
    return contents->image.dim(i);
}

buffer_t *Buffer::raw_buffer() const {
    return contents->image.raw_buffer();
}

size_t Buffer::size_in_bytes() const {
    return contents->image.size_in_bytes();
}

uint8_t *Buffer::host_ptr() const {
    return raw_buffer()->host;
}

Expr Buffer::operator()(const std::vector<Expr> &args) const {
    // Cast the inputs to int32
    std::vector<Expr> int_args;
    for (Expr e : args) {
        user_assert(Int(32).can_represent(e.type()))
            << "Args to a call to an Image must be representable as 32-bit integers.\n";
        if (equal(e, _)) {
            // Expand the _ into the appropriate number of implicit vars.
            int missing_dimensions = dimensions() - (int)args.size() + 1;
            for (int i = 0; i < missing_dimensions; i++) {
                int_args.push_back(Var::implicit(i));
            }
        } else if (e.type() == Int(32)) {
            int_args.push_back(e);
        } else {
            int_args.push_back(cast<int>(e));
        }
    }
    return Internal::Call::make(*this, int_args);
}

}
}
