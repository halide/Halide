#ifndef TYPE_H
#define TYPE_H

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


}

#endif
