#ifndef HALIDE_PARAMETER_H
#define HALIDE_PARAMETER_H

#include <string>

namespace Halide {
namespace Internal {

using std::string;

struct ParameterContents {
    mutable RefCount ref_count;
    Type type;
    string name;
    Buffer buffer;
    uint64_t data;
    ParameterContents(Type t, const string &n) : type(t), name(n), buffer(Buffer()), data(0) {
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

    Parameter(Type t) : 
        contents(new ParameterContents(t, unique_name('p'))) {
    }

    Parameter(Type t, const string &n) : 
        contents(new ParameterContents(t, n)) {
    }

    Type type() const {
        assert(contents.defined());
        return contents.ptr->type;
    }

    const string &name() const {
        assert(contents.defined());
        return contents.ptr->name;
    }

    template<typename T>
    T get_scalar() const {
        assert(contents.defined());
        return contents.ptr->as<T>();
    }

    Buffer get_buffer() const {
        assert(contents.defined());
        return contents.ptr->buffer;
    }

    template<typename T>
    void set_scalar(T val) {
        assert(contents.defined());
        contents.ptr->as<T>() = val;
    }

    void set_buffer(Buffer b) {
        assert(contents.defined());
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
