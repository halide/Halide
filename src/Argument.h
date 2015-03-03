#ifndef HALIDE_ARGUMENT_H
#define HALIDE_ARGUMENT_H

#include <string>
#include <vector>

#include "Error.h"
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
     * buffer pointer.
     *
     * If kind == Scalar, then type fully encodes the expected type
     * of the scalar argument.
     *
     * If kind == Buffer, then type.bytes() should be used to determine
     * elem_size of the buffer; additionally, type.code *should* reflect
     * the expected interpretation of the buffer data (e.g. float vs int),
     * but there is no runtime enforcement of this at present.
     */
    enum Kind {
        Scalar,
        Buffer
    };
    Kind kind;

    /** If kind == Buffer is true, this is the dimensionality of the buffer.
     * If kind == Scalar is false, this value is ignored (and should always be set to zero) */
    uint8_t dimensions;

    /** If this is a scalar parameter, then this is its type.
     *
     * If this is a buffer parameter, this is used to determine elem_size
     * of the buffer_t.
     *
     * Note that type.width should always be 1 here. */
    Type type;

    /** If this is a scalar parameter, then these are its default, min, max values.
     * By default, they are left unset, implying "no default, no min, no max". */
    Expr def, min, max;

    Argument() : kind(Scalar), dimensions(0) {}
    Argument(const std::string &_name, Kind _kind, const Type &_type, uint8_t _dimensions,
                Expr _def = Expr(),
                Expr _min = Expr(),
                Expr _max = Expr()) :
        name(_name), kind(_kind), dimensions(_dimensions), type(_type), def(_def), min(_min), max(_max) {
        user_assert(!(kind == Scalar && dimensions != 0))
            << "Scalar Arguments must specify dimensions of 0";
        user_assert(!(kind == Buffer && def.defined()))
            << "Scalar default must not be defined for Buffer Arguments";
        user_assert(!(kind == Buffer && min.defined()))
            << "Scalar min must not be defined for Buffer Arguments";
        user_assert(!(kind == Buffer && max.defined()))
            << "Scalar max must not be defined for Buffer Arguments";
    }

    bool is_buffer() const { return kind == Buffer; }
};

namespace Internal {

/** struct to pass information about Arguments around internally. */
struct ArgInfo {
    /** The Arguments to a halide statement; these must
     * always be the input Arguments (if any) followed by the
     * output Arguments (>= 1) */
    std::vector<Argument> args;

    /** It is expected that the final "num_outputs"
     * entries in args are outputs (should be 1 unless the statement
     * returns a Tuple) */
    int num_outputs;

    // Note that since all valid pipelines have at least one output,
    // initializing to zero makes for a known invalid value
    ArgInfo() : num_outputs(0) {}
};


}  // namespace Internal

}

#endif
