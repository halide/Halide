#ifndef HALIDE_PARAMETER_H
#define HALIDE_PARAMETER_H

#include <string>

namespace Halide {
namespace Internal {

struct ParameterContents {
    mutable RefCount ref_count;
    Type type;
    bool is_buffer;
    std::string name;
    Buffer buffer;
    uint64_t data;
    ParameterContents(Type t, bool b, const std::string &n) : type(t), is_buffer(b), name(n), buffer(Buffer()), data(0) {
    }

    template<typename T>
    T &as() {
        assert(type == type_of<T>());
        return *((T *)(&data));
    }
};

class Parameter {
    IntrusivePtr<ParameterContents> contents;
public:
    Parameter() : contents(NULL) {}

    Parameter(Type t, bool b) : 
        contents(new ParameterContents(t, b, unique_name('p'))) {
    }

    Parameter(Type t, bool b, const std::string &n) : 
        contents(new ParameterContents(t, b, n)) {
    }

    Type type() const {
        assert(contents.defined());
        return contents.ptr->type;
    }

    const std::string &name() const {
        assert(contents.defined());
        return contents.ptr->name;
    }

    bool is_buffer() const {
        assert(contents.defined());
        return contents.ptr->is_buffer;
    }

    template<typename T>
    T get_scalar() const {
        assert(contents.defined() && !contents.ptr->is_buffer);
        return contents.ptr->as<T>();
    }

    Buffer get_buffer() const {
        assert(contents.defined() && contents.ptr->is_buffer);
        return contents.ptr->buffer;
    }

    template<typename T>
    void set_scalar(T val) {
        assert(contents.defined() && !contents.ptr->is_buffer);
        contents.ptr->as<T>() = val;
    }

    void set_buffer(Buffer b) {
        assert(contents.defined() && contents.ptr->is_buffer);
        if (b.defined()) assert(contents.ptr->type == b.type());
        contents.ptr->buffer = b;
    }

    const void *get_scalar_address() const {
        assert(contents.defined());
        return &contents.ptr->data;
    }

    bool defined() const {
        return contents.defined();
    }
};

}
}

#endif
