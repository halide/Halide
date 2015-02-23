#ifndef HALIDE_ARGUMENT_H
#define HALIDE_ARGUMENT_H

#include <string>
#include "Expr.h"
#include "Type.h"

/** \file
 * Defines a type used for expressing the type signature of a
 * generated halide pipeline
 */

namespace Halide {

/**
 * A struct representing an argument to a halide-generated
 * function. Used for specifying the function signature of
 * generated code.
 */
struct Argument {
    /** The name of the argument */
    std::string name;

    /** An argument is either a primitive type (for parameters), or a
     * buffer pointer. If 'is_buffer' is true, then 'type' should be
     * ignored.
     */
    bool is_buffer;

    /** If this is a scalar parameter, then this is its type */
    Type type;

    Argument() : is_buffer(false) {}
    Argument(const std::string &_name, bool _is_buffer, Type _type) :
        name(_name), is_buffer(_is_buffer), type(_type) {
    }
};
}

#endif
