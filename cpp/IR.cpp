#include "IR.h"

namespace HalideInternal {

    Type Int(int bits, int width) {
        Type t;
        t.t = Type::Int;
        t.bits = bits;
        t.width = width;
        return t;
    }
    
    Type UInt(int bits, int width) {
        Type t;
        t.t = Type::UInt;
        t.bits = bits;
        t.width = width;
        return t;
    }

    Type Float(int bits, int width) {
        Type t;
        t.t = Type::Float;
        t.bits = bits;
        t.width = width;
        return t;
    }
}
