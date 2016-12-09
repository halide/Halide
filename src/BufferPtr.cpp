#include "BufferPtr.h"
#include "Debug.h"
#include "Error.h"
#include "JITModule.h"
#include "runtime/HalideRuntime.h"
#include "Target.h"
#include "Var.h"
#include "IREquality.h"
#include "IROperator.h"

#include <mutex>

namespace Halide {
namespace Internal {

struct BufferContents {
    Buffer<> image;
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
std::string make_buffer_name(const std::string &n, const Buffer<> &image) {
    if (n.empty()) {
        // Embedded images are deduped by name, so it's important that
        // the same image always gets the same name.
        static std::mutex name_lock;
        std::lock_guard<std::mutex> lock_guard(name_lock);
        static std::map<const void *, std::string> buffer_names;
        auto it = buffer_names.find(image.data());
        if (it == buffer_names.end()) {
            std::string name = unique_name('b');
            buffer_names[image.data()] = name;
            return name;
        } else {
            return it->second;
        }
    } else {
        return n;
    }
}
}

BufferPtr::BufferPtr(const Buffer<> &buf, std::string name) :
    contents(new Internal::BufferContents) {
    contents->image = Buffer<>(buf);
    contents->name = make_buffer_name(name, contents->image);
}

BufferPtr::BufferPtr(Type t, const std::vector<int> &size, std::string name) :
    contents(new Internal::BufferContents) {
    contents->image = Buffer<>(t, size);
    contents->name = make_buffer_name(name, contents->image);
}

bool BufferPtr::same_as(const BufferPtr &other) const {
    return contents.same_as(other.contents);
}

Buffer<> &BufferPtr::get() {
    return contents->image;
}

const Buffer<> &BufferPtr::get() const {
    return contents->image;
}

bool BufferPtr::defined() const {
    return contents->image;
}

const std::string &BufferPtr::name() const {
    return contents->name;
}

Type BufferPtr::type() const {
    return contents->image.type();
}

int BufferPtr::dimensions() const {
    return contents->image.dimensions();
}

Buffer<>::Dimension BufferPtr::dim(int i) const {
    return contents->image.dim(i);
}

int BufferPtr::min(int i) const {
    return dim(i).min();
}

int BufferPtr::extent(int i) const {
    return dim(i).extent();
}

int BufferPtr::stride(int i) const {
    return dim(i).stride();
}

halide_buffer_t *BufferPtr::raw_buffer() const {
    return contents->image.raw_buffer();
}

size_t BufferPtr::size_in_bytes() const {
    return contents->image.size_in_bytes();
}

uint8_t *BufferPtr::host_ptr() const {
    return raw_buffer()->host;
}

Expr BufferPtr::operator()(const std::vector<Expr> &args) const {
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
