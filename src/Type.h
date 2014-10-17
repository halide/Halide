#ifndef HALIDE_TYPE_H
#define HALIDE_TYPE_H

#include <stdint.h>
#include "Util.h"

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
    /** The basic type code: signed integer, unsigned integer, or floating point */
    enum TypeCode {
        Int,  //!< signed integers
        UInt, //!< unsigned integers
        Float, //!< floating point numbers
        Handle //!< opaque pointer type (void *)
    } code;

    /** The number of bits of precision of a single scalar value of this type. */
    int bits;

    /** The number of bytes required to store a single scalar value of this type. Ignores vector width. */
    int bytes() const {return (bits + 7) / 8;}

    /** How many elements (if a vector type). Should be 1 for scalar types. */
    int width;

    /** Is this type boolean (represented as UInt(1))? */
    bool is_bool() const {return code == UInt && bits == 1;}

    /** Is this type a vector type? (width > 1) */
    bool is_vector() const {return width > 1;}

    /** Is this type a scalar type? (width == 1) */
    bool is_scalar() const {return width == 1;}

    /** Is this type a floating point type (float or double). */
    bool is_float() const {return code == Float;}

    /** Is this type a signed integer type? */
    bool is_int() const {return code == Int;}

    /** Is this type an unsigned integer type? */
    bool is_uint() const {return code == UInt;}

    /** Is this type an opaque handle type (void *) */
    bool is_handle() const {return code == Handle;}

    /** Compare two types for equality */
    bool operator==(const Type &other) const {
        return code == other.code && bits == other.bits && width == other.width;
    }

    /** Compare two types for inequality */
    bool operator!=(const Type &other) const {
        return code != other.code || bits != other.bits || width != other.width;
    }

    /** Produce a vector of this type, with 'width' elements */
    Type vector_of(int w) const {
        Type type = {code, bits, w};
        return type;
    }

    /** Produce the type of a single element of this vector type */
    Type element_of() const {
        Type type = {code, bits, 1};
        return type;
    }

    /** Can this type represent all values of another type? */
    EXPORT bool can_represent(Type other) const;

    /** Return an integer which is the maximum value of this type. */
    EXPORT int imax() const;

    /** Return an expression which is the maximum value of this type */
    EXPORT Expr max() const;

    /** Return an integer which is the minimum value of this type */
    EXPORT int imin() const;

    /** Return an expression which is the minimum value of this type */
    EXPORT Expr min() const;
};

/** Constructing a signed integer type */
inline Type Int(int bits, int width = 1) {
    Type t;
    t.code = Type::Int;
    t.bits = bits;
    t.width = width;
    return t;
}

/** Constructing an unsigned integer type */
inline Type UInt(int bits, int width = 1) {
    Type t;
    t.code = Type::UInt;
    t.bits = bits;
    t.width = width;
    return t;
}

/** Construct a floating-point type */
inline Type Float(int bits, int width = 1) {
    Type t;
    t.code = Type::Float;
    t.bits = bits;
    t.width = width;
    return t;
}

/** Construct a boolean type */
inline Type Bool(int width = 1) {
    return UInt(1, width);
}

/** Construct a handle type */
inline Type Handle(int width = 1) {
    Type t;
    t.code = Type::Handle;
    t.bits = 64; // All handles are 64-bit for now
    t.width = width;
    return t;
}

namespace {
template<typename T>
struct type_of_helper;

template<typename T>
struct type_of_helper<T *> {
    operator Type() {
        return Handle();
    }
};

template<>
struct type_of_helper<float> {
    operator Type() {return Float(32);}
};

template<>
struct type_of_helper<double> {
    operator Type() {return Float(64);}
};

template<>
struct type_of_helper<uint8_t> {
    operator Type() {return UInt(8);}
};

template<>
struct type_of_helper<uint16_t> {
    operator Type() {return UInt(16);}
};

template<>
struct type_of_helper<uint32_t> {
    operator Type() {return UInt(32);}
};

template<>
struct type_of_helper<uint64_t> {
    operator Type() {return UInt(64);}
};

template<>
struct type_of_helper<int8_t> {
    operator Type() {return Int(8);}
};

template<>
struct type_of_helper<int16_t> {
    operator Type() {return Int(16);}
};

template<>
struct type_of_helper<int32_t> {
    operator Type() {return Int(32);}
};

template<>
struct type_of_helper<int64_t> {
    operator Type() {return Int(64);}
};

template<>
struct type_of_helper<bool> {
    operator Type() {return Bool();}
};
}

/** Construct the halide equivalent of a C type */
template<typename T> Type type_of() {
    return Type(type_of_helper<T>());
}

}

#endif
