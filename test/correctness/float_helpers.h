#ifndef FLOAT_HELPERS_H
#define FLOAT_HELPERS_H
#include <cinttypes>
uint32_t bits_from_float(float v) {
    union {
        float asFloat;
        uint32_t asUInt;
    } out;
    out.asFloat = v;
    return out.asUInt;
}

uint64_t bits_from_double(double v) {
    union {
        double asDouble;
        uint64_t asUInt;
    } out;
    out.asDouble = v;
    return out.asUInt;
}

float float_from_bits(uint32_t bits) {
    union {
        float asFloat;
        uint32_t asUInt;
    } out;
    out.asUInt = bits;
    return out.asFloat;
}

double double_from_bits(uint64_t bits) {
    union {
        double asDouble;
        uint64_t asUInt;
    } out;
    out.asUInt = bits;
    return out.asDouble;
}

#endif
