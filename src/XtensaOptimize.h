#ifndef HALIDE_XTENSA_OPTIMIZE_H
#define HALIDE_XTENSA_OPTIMIZE_H

#include "Expr.h"

namespace Halide {

struct Target;

namespace Internal {

template<typename T>
HALIDE_ALWAYS_INLINE bool is_native_xtensa_vector(const Type &t) {
    return false;
}

// halide_type_t::operator== should inline as a single load-and-compare of u32 values
template<>
HALIDE_ALWAYS_INLINE bool is_native_xtensa_vector<int8_t>(const Type &t) {
    return t == (halide_type_t)Int(8, 64);
}

template<>
HALIDE_ALWAYS_INLINE bool is_native_xtensa_vector<uint8_t>(const Type &t) {
    return t == (halide_type_t)UInt(8, 64);
}

template<>
HALIDE_ALWAYS_INLINE bool is_native_xtensa_vector<int16_t>(const Type &t) {
    return t == (halide_type_t)Int(16, 32);
}

template<>
HALIDE_ALWAYS_INLINE bool is_native_xtensa_vector<uint16_t>(const Type &t) {
    return t == (halide_type_t)UInt(16, 32);
}

template<>
HALIDE_ALWAYS_INLINE bool is_native_xtensa_vector<int32_t>(const Type &t) {
    return t == (halide_type_t)Int(32, 16);
}

template<>
HALIDE_ALWAYS_INLINE bool is_native_xtensa_vector<uint32_t>(const Type &t) {
    return t == (halide_type_t)UInt(32, 16);
}

template<>
HALIDE_ALWAYS_INLINE bool is_native_xtensa_vector<float>(const Type &t) {
    return t == (halide_type_t)Float(32, 16);
}

bool is_native_vector_type(const Type &t);
bool is_double_native_vector_type(const Type &t);

Type get_native_xtensa_vector(const Type &t);

std::string suffix_for_type(Type t);

Stmt match_xtensa_patterns(const Stmt &s, const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif
