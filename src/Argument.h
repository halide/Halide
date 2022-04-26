#ifndef HALIDE_ARGUMENT_H
#define HALIDE_ARGUMENT_H

/** \file
 * Defines a type used for expressing the type signature of a
 * generated halide pipeline
 */

#include "Expr.h"
#include "Type.h"
#include "runtime/HalideRuntime.h"

namespace Halide {

template<typename T, int Dims>
class Buffer;

struct ArgumentEstimates {
    /** If this is a scalar argument, then these are its default, min, max, and estimated values.
     * For buffer arguments, all should be undefined. */
    Expr scalar_def, scalar_min, scalar_max, scalar_estimate;

    /** If this is a buffer argument, these are the estimated min and
     * extent for each dimension.  If there are no estimates,
     * buffer_estimates.size() can be zero; otherwise, it must always
     * equal the dimensions */
    Region buffer_estimates;

    bool operator==(const ArgumentEstimates &rhs) const;
};

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
     * If kind == InputScalar, then type fully encodes the expected type
     * of the scalar argument.
     *
     * If kind == InputBuffer|OutputBuffer, then type.bytes() should be used
     * to determine* elem_size of the buffer; additionally, type.code *should*
     * reflect the expected interpretation of the buffer data (e.g. float vs int),
     * but there is no runtime enforcement of this at present.
     */
    enum Kind {
        InputScalar = halide_argument_kind_input_scalar,
        InputBuffer = halide_argument_kind_input_buffer,
        OutputBuffer = halide_argument_kind_output_buffer
    };
    Kind kind = InputScalar;

    /** If kind == InputBuffer|OutputBuffer, this is the dimensionality of the buffer.
     * If kind == InputScalar, this value is ignored (and should always be set to zero) */
    uint8_t dimensions = 0;

    /** If this is a scalar parameter, then this is its type.
     *
     * If this is a buffer parameter, this this is the type of its
     * elements.
     *
     * Note that type.lanes should always be 1 here. */
    Type type;

    /* The estimates (if any) and default/min/max values (if any) for this Argument. */
    ArgumentEstimates argument_estimates;

    Argument() = default;
    Argument(const std::string &_name, Kind _kind, const Type &_type, int _dimensions,
             const ArgumentEstimates &argument_estimates);

    // Not explicit, so that you can put Buffer in an argument list,
    // to indicate that it shouldn't be baked into the object file,
    // but instead received as an argument at runtime
    template<typename T, int Dims>
    Argument(Buffer<T, Dims> im)
        : name(im.name()),
          kind(InputBuffer),
          dimensions(im.dimensions()),
          type(im.type()) {
    }

    bool is_buffer() const {
        return kind == InputBuffer || kind == OutputBuffer;
    }
    bool is_scalar() const {
        return kind == InputScalar;
    }

    bool is_input() const {
        return kind == InputScalar || kind == InputBuffer;
    }
    bool is_output() const {
        return kind == OutputBuffer;
    }

    bool operator==(const Argument &rhs) const {
        return name == rhs.name &&
               kind == rhs.kind &&
               dimensions == rhs.dimensions &&
               type == rhs.type &&
               argument_estimates == rhs.argument_estimates;
    }
};

}  // namespace Halide

#endif
