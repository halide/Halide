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
 * be vectors of the same (by setting the 'lanes' field to something
 * larger than one). Front-end code shouldn't use vector
 * types. Instead vectorize a function. */
struct Type {
  private:
    halide_type_t type;

  public:
    /** Aliases for halide_type_code_t values for legacy compatibility
     * and to match the Halide internal C++ style. */
    // @{
    static const halide_type_code_t Int = halide_type_int;
    static const halide_type_code_t UInt = halide_type_uint;
    static const halide_type_code_t Float = halide_type_float;
    static const halide_type_code_t Handle = halide_type_handle;
    // @}

    /** The number of bytes required to store a single scalar value of this type. Ignores vector lanes. */
    int bytes() const {return (bits() + 7) / 8;}

    // Default ctor initializes everything to predictable-but-unlikely values
    Type() : type(Handle, 0, 0) {}

    
    /** Construct a runtime representation of a Halide type from:
     * code: The fundamental type from an enum.
     * bits: The bit size of one element.
     * lanes: The number of vector elements in the type. */
    Type(halide_type_code_t code, uint8_t bits, int lanes) 
        : type(code, (uint8_t)bits, (uint16_t)lanes) {
    }

    /** Trivial copy constructor. */
    Type(const Type &that) = default;

    /** Type is a wrapper around halide_type_t with more methods for use
     * inside the compiler. This simply constructs the wrapper around
     * the runtime value. */
    Type(const halide_type_t &that) : type(that) {}

    /** Unwrap the runtime halide_type_t for use in runtime calls, etc.
     * Representation is exactly equivalent. */
    operator halide_type_t() const { return type; }

    /** Return the underlying data type of an element as an enum value. */
    halide_type_code_t code() const { return (halide_type_code_t)type.code; }

    /** Return the bit size of a single element of this type. */
    int bits() const { return type.bits; }

    /** Return the number of vector elements in this type. */
    int lanes() const { return type.lanes; }

    /** Return Type with same number of bits and lanes, but new_code for a type code. */
    Type with_code(halide_type_code_t new_code) const {
        return Type(new_code, bits(), lanes());
    }

    /** Return Type with same type code and lanes, but new_bits for the number of bits. */
    Type with_bits(uint8_t new_bits) const {
        return Type(code(), new_bits, lanes());
    }

    /** Return Type with same type code and number of bits,
     * but new_lanes for the number of vector lanes. */
    Type with_lanes(uint16_t new_lanes) const {
        return Type(code(), bits(), new_lanes);
    }

    /** Is this type boolean (represented as UInt(1))? */
    bool is_bool() const {return code() == UInt && bits() == 1;}

    /** Is this type a vector type? (lanes() != 1).
     * TODO(abadams): Decide what to do for lanes() == 0. */
    bool is_vector() const {return lanes() != 1;}

    /** Is this type a scalar type? (lanes() == 1).
     * TODO(abadams): Decide what to do for lanes() == 0. */
    bool is_scalar() const {return lanes() == 1;}

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
        return code() == other.code() && bits() == other.bits() && lanes() == other.lanes();
    }

    /** Compare two types for inequality */
    bool operator!=(const Type &other) const {
        return code() != other.code() || bits() != other.bits() || lanes() != other.lanes();
    }

    /** Produce the scalar type (that of a single element) of this vector type */
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
inline Type Int(int bits, int lanes = 1) {
    return Type(Type::Int, bits, lanes);
}

/** Constructing an unsigned integer type */
inline Type UInt(int bits, int lanes = 1) {
    return Type(Type::UInt, bits, lanes);
}

/** Construct a floating-point type */
inline Type Float(int bits, int lanes = 1) {
    return Type(Type::Float, bits, lanes);
}

/** Construct a boolean type */
inline Type Bool(int lanes = 1) {
    return UInt(1, lanes);
}

/** Construct a handle type */
inline Type Handle(int lanes = 1) {
    return Type(Type::Handle, 64, lanes);
}

/** Construct the halide equivalent of a C type */
template<typename T> Type type_of() {
    return Type(halide_type_of<T>());
}

}

#endif
