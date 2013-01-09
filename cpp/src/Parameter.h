#ifndef HALIDE_PARAMETER_H
#define HALIDE_PARAMETER_H

/** \file
 * Defines the internal representation of parameters to halide piplines 
 */

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

/** A reference-counted handle to a parameter to a halide
 * pipeline. May be a scalar parameter or a buffer */
class Parameter {
    IntrusivePtr<ParameterContents> contents;
public:
    /** Construct a new undefined handle */
    Parameter() : contents(NULL) {}

    /** Construct a new parameter of the given type. If the second
     * argument is true, this is a buffer parameter, otherwise, it is
     * a scalar parameter. The parameter will be given a unique
     * auto-generated name. */
    Parameter(Type t, bool is_buffer) : 
        contents(new ParameterContents(t, is_buffer, unique_name('p'))) {
    }
    
    /** Construct a new parameter of the given type with name given by
     * the third argument. If the second argument is true, this is a
     * buffer parameter, otherwise, it is a scalar parameter. The
     * parameter will be given a unique auto-generated name. */
    Parameter(Type t, bool is_buffer, const std::string &name) : 
        contents(new ParameterContents(t, is_buffer, name)) {
    }

    /** Get the type of this parameter */
    Type type() const {
        assert(contents.defined());
        return contents.ptr->type;
    }

    /** Get the name of this parameter */
    const std::string &name() const {
        assert(contents.defined());
        return contents.ptr->name;
    }

    /** Does this parameter refer to a buffer/image? */
    bool is_buffer() const {
        assert(contents.defined());
        return contents.ptr->is_buffer;
    }

    /** If the parameter is a scalar parameter, get its currently
     * bound value. Only relevant when jitting */
    template<typename T>
    T get_scalar() const {
        assert(contents.defined() && !contents.ptr->is_buffer);
        return contents.ptr->as<T>();
    }

    /** If the parameter is a buffer parameter, get its currently
     * bound buffer. Only relevant when jitting */
    Buffer get_buffer() const {
        assert(contents.defined() && contents.ptr->is_buffer);
        return contents.ptr->buffer;
    }

    /** If the parameter is a scalar parameter, set its current
     * value. Only relevant when jitting */
    template<typename T>
    void set_scalar(T val) {
        assert(contents.defined() && !contents.ptr->is_buffer);
        contents.ptr->as<T>() = val;
    }

    /** If the parameter is a buffer parameter, set its current
     * value. Only relevant when jitting */
    void set_buffer(Buffer b) {
        assert(contents.defined() && contents.ptr->is_buffer);
        if (b.defined()) assert(contents.ptr->type == b.type());
        contents.ptr->buffer = b;
    }

    /** Get the pointer to the current value of the scalar
     * parameter. For a given parameter, this address will never
     * change. Only relevant when jitting. */
    const void *get_scalar_address() const {
        assert(contents.defined());
        return &contents.ptr->data;
    }

    /** Tests if this handle is non-NULL */
    bool defined() const {
        return contents.defined();
    }
};

}
}

#endif
