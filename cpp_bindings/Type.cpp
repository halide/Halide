#include "Type.h"

namespace Halide {

    ML_FUNC1(makeFloatType);
    ML_FUNC1(makeIntType);
    ML_FUNC1(makeUIntType);

    template<>
    Type TypeOf<float>() {
        return Float(32);
    }

    template<>
    Type TypeOf<double>() {
        return Float(64);
    }

    template<>
    Type TypeOf<unsigned char>() {
        return UInt(8);
    }

    template<>
    Type TypeOf<unsigned short>() {
        return UInt(16);
    }

    template<>
    Type TypeOf<unsigned int>() {
        return UInt(32);
    }

    template<>
    Type TypeOf<bool>() {
        return Int(1);
    }

    template<>
    Type TypeOf<char>() {
        return Int(8);
    }

    template<>
    Type TypeOf<short>() {
        return Int(16);
    }

    template<>
    Type TypeOf<int>() {
        return Int(32);
    }

    template<>
    Type TypeOf<signed char>() {
        return Int(8);
    }

    Type Float(unsigned char bits) {
        Type t;
        t.mlval = makeFloatType(bits);
        t.bits = bits;
        t.code = Type::FLOAT;
        return t;
    }

    Type Int(unsigned char bits) {
        Type t;
        t.mlval = makeIntType(bits);
        t.bits = bits;
        t.code = Type::INT;
        return t;
    }

    Type UInt(unsigned char bits) {
        Type t;
        t.mlval = makeUIntType(bits);
        t.bits = bits;
        t.code = Type::UINT;
        return t;
    }

}
