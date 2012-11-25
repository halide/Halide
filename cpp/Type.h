#ifndef TYPE_H
#define TYPE_H

namespace HalideInternal {
    struct Type {
        enum {Int, UInt, Float} t;
        int bits;
        int width;        
        bool is_bool() const {return t == UInt && bits == 1;}
        bool is_vector() const {return width > 1;}
        bool is_scalar() const {return width == 1;}
        bool operator==(const Type &other) const {
            return t == other.t && bits == other.bits && width == other.width;
        }
        bool is_float() const {return t == Float;}
        bool is_int() const {return t == Int;}
        bool is_uint() const {return t == UInt;}
        static Type vector_of(Type t, int width) {
            t.width = width;
            return t;
        }
        static Type element_of(Type t) {
            t.width = 1;
            return t;
        }
    };

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
