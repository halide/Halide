#ifndef HALIDE_TYPE_H
#define HALIDE_TYPE_H

namespace Halide {
/* Types in the halide type system. They can be ints, unsigned
 * ints, or floats of various bit-widths (the 'bits' field). They
 * can also be vectors of the same (by setting the 'width' field
 * to something larger than one). */
struct Type {
    enum {Int, UInt, Float} t;
    int bits;
    int width;        

    /* Some helper functions to ask common questions about a type */
    bool is_bool() const {return t == UInt && bits == 1;}
    bool is_vector() const {return width > 1;}
    bool is_scalar() const {return width == 1;}
    bool is_float() const {return t == Float;}
    bool is_int() const {return t == Int;}
    bool is_uint() const {return t == UInt;}

    /* You can compare types for equality */
    bool operator==(const Type &other) const {
        return t == other.t && bits == other.bits && width == other.width;
    }

    bool operator!=(const Type &other) const {
        return t != other.t || bits != other.bits || width != other.width;
    }

    /* Produce a vector of this type, with 'width' elements */
    Type vector_of(int w) const {
        Type type = {t, bits, w};
        return type;
    }

    /* Produce the type of a single element of this vector type */
    Type element_of() const {
        Type type = {t, bits, 1};
        return type;
    }
};

/* Convenience functions for constructing types. */
inline Type Int(int bits, int width = 1) {
    Type t;
    t.t = Type::Int;
    t.bits = bits;
    t.width = width;
    return t;
}

inline Type UInt(int bits, int width = 1) {
    Type t;
    t.t = Type::UInt;
    t.bits = bits;
    t.width = width;
    return t;
}

inline Type Float(int bits, int width = 1) {
    Type t;
    t.t = Type::Float;
    t.bits = bits;
    t.width = width;
    return t;
}

inline Type Bool(int width = 1) {
    return UInt(1, width);
}

template<typename T> Type type_of();
template<> inline Type type_of<float>() {return Float(32);}
template<> inline Type type_of<double>() {return Float(64);}
template<> inline Type type_of<unsigned char>() {return UInt(8);}
template<> inline Type type_of<unsigned short>() {return UInt(16);}
template<> inline Type type_of<unsigned int>() {return UInt(32);}
template<> inline Type type_of<bool>() {return Bool();}
template<> inline Type type_of<char>() {return Int(8);}
template<> inline Type type_of<short>() {return Int(16);}
template<> inline Type type_of<int>() {return Int(32);}
template<> inline Type type_of<signed char>() {return Int(8);}

}

#endif
