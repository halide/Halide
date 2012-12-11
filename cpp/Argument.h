#ifndef ARGUMENT_H
#define ARGUMENT_H

#include <string>
#include "Type.h"

namespace Halide { 
namespace Internal {

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
};
}
}

#endif
