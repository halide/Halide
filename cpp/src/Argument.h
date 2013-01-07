#ifndef HALIDE_ARGUMENT_H
#define HALIDE_ARGUMENT_H

#include <string>
#include "Type.h"

/** \file 
 * A type representing top-level arguments to a halide pipeline 
 */

namespace Halide { 

/**
 * A struct representing an argument to a halide-generated
 * function. Used for specifying the function signature of
 * generated code. 
 */
struct Argument {
    std::string name;        
            
    /* An argument is either a primitive type (for parameters), or a
     * buffer pointer. If 'is_buffer' is true, then 'type' should be
     * ignored. 
     */
    bool is_buffer;
    Type type;

    Argument() {}
    Argument(const std::string &n, bool b, Type t) : name(n), is_buffer(b), type(t) {}
};
}

#endif
