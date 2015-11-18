#ifndef HALIDE_TYPE_H
#define HALIDE_TYPE_H

#include <stdint.h>
#include "runtime/HalideRuntime.h"
#include "Util.h"
#include "Float16.h"

/** \file
 * Defines halide types
 */

namespace Halide {

struct Expr;

/** Types in the halide type system. They can be ints, unsigned ints,
 * or floats of various bit-widths (the 'bits' field). They can also
 * be vectors of the same (by setting the 'width' field to something
 * larger than one). Front-end code shouldn't use vector
 * types. Instead vectorize a function. */
struct Type {
  private:
    halide_type_t type;

  public:
    /** Aliases for halide_type_code_t values for legacy compatibility
     * and to match the Halide internal C++ style. */
    // @{
    static constexpr halide_type_code_t Int = halide_type_int;
    static constexpr halide_type_code_t UInt = halide_type_uint;
    static constexpr halide_type_code_t Float = halide_type_float;
    static constexpr halide_type_code_t Handle = halide_type_handle;
    // @}

    /** The number of bytes required to store a single scalar value of this type. Ignores vector width. */
    int bytes() const {return (bits() + 7) / 8;}

    // Default ctor initializes everything to predictable-but-unlikely values
    Type() : type(Handle, 0, 0) {}

    Type(halide_type_code_t code, int bits, int width) : type(code, bits, width) {}

    Type(const Type &that) : type(that) {}

    Type(const halide_type_t &that) : type(that) {}

    operator halide_type_t() const { return type; }

    halide_type_code_t code() const { return type.code; }
    uint32_t bits() const { return type.bits; }
    uint32_t width() const { return type.width; }

    /** Return Type with same number of bits and width, but new_codefor a type code. */
    Type with_code(halide_type_code_t new_code) const {
        return Type(new_code, bits(), width());
    }

    /** Return Type with same type code and width, but new_bits for the number of bits. */
    Type with_bits(uint8_t new_bits) const {
        return Type(code(), new_bits, width());
    }

    /** Return Type with same type code and number of bits, but new_width for the vector width. */
    // TODO(zalman): Should this be called broadcast?
    Type with_width(uint16_t new_width) const {
        return Type(code(), bits(), new_width);
    }

    /** Is this type boolean (represented as UInt(1))? */
    bool is_bool() const {return code() == UInt && bits() == 1;}

    /** Is this type a vector type? (width > 1) */
    bool is_vector() const {return width() != 1;}

    /** Is this type a scalar type? (width() == 1) */
    bool is_scalar() const {return width() == 1;}

    /** Is this type a floating point type (float or double). */
    bool is_float() const {return code() == Float;}

    /** Is this type a signed integer type? */
    bool is_int() const {return code() == Int;}

    /** Is this type an unsigned integer type? */
    bool is_uint() const {return code() == UInt;}

    /** Is this type an opaque handle type (void *) */
    bool is_handle() const {return code() == Handle;}

    /** Compare two types for equality */
    bool operator==(const Type &other) const {
        return code() == other.code() && bits() == other.bits() && width() == other.width();
    }

    /** Compare two types for inequality */
    bool operator!=(const Type &other) const {
        return code() != other.code() || bits() != other.bits() || width() != other.width();
    }

    /** Produce a vector of this type, with 'width' elements */
    Type vector_of(int w) const {
        return Type(code(), bits(), w);
    }

    /** Produce the type of a single element of this vector type */
    Type element_of() const {
        return Type(code(), bits(), 1);
    }

    /** Can this type represent all values of another type? */
    EXPORT bool can_represent(Type other) const;

    /** Can this type represent a particular constant? */
    // @{
    EXPORT bool can_represent(double x) const;
    EXPORT bool can_represent(int64_t x) const;
    EXPORT bool can_represent(uint64_t x) const;
    // @}

    /** Check if an integer constant value is the maximum or minimum
     * representable value for this type. */
    // @{
    EXPORT bool is_max(uint64_t) const;
    EXPORT bool is_max(int64_t) const;
    EXPORT bool is_min(uint64_t) const;
    EXPORT bool is_min(int64_t) const;
    // @}

    /** Return an expression which is the maximum value of this type */
    EXPORT Expr max() const;

    /** Return an expression which is the minimum value of this type */
    EXPORT Expr min() const;
};

/** Constructing a signed integer type */
inline Type Int(int bits, int width = 1) {
    return Type(Type::Int, bits, width);
}

/** Constructing an unsigned integer type */
inline Type UInt(int bits, int width = 1) {
    return Type(Type::UInt, bits, width);
}

/** Construct a floating-point type */
inline Type Float(int bits, int width = 1) {
    return Type(Type::Float, bits, width);
}

/** Construct a boolean type */
inline Type Bool(int width = 1) {
    return UInt(1, width);
}

/** Construct a handle type */
inline Type Handle(int width = 1) {
    return Type(Type::Handle, 64, width);
}

/** Construct the halide equivalent of a C type */
template<typename T> Type type_of() {
    return Type(halide_type_of<T>());
}

}

#endif
