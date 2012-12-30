#ifndef ARGUMENT_H
#define ARGUMENT_H

#include <string>
#include "Type.h"

namespace Halide { 

/* A struct representing an argument to a halide-generated
 * function. Used for specifying the function signature of
 * generated code. */
struct Argument {
    std::string name;        
            
    /* It's either a primitive type (for uniforms), or a buffer
     * pointer. If 'is_buffer' is true, then 'type' should be ignored.
     */
    bool is_buffer;
    Type type;

    Argument() {}
    Argument(const std::string &n, bool b, Type t) : name(n), is_buffer(b), type(t) {}
};
}

#endif
