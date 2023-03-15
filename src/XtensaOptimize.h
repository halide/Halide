#ifndef HALIDE_XTENSA_OPTIMIZE_H
#define HALIDE_XTENSA_OPTIMIZE_H

#include "Expr.h"

namespace Halide {

struct Target;

namespace Internal {

template<typename T>
bool is_native_xtensa_vector(const Type &t, const Target &target) {
    return false;
}

template<>
bool is_native_xtensa_vector<int8_t>(const Type &t, const Target &target);

template<>
bool is_native_xtensa_vector<uint8_t>(const Type &t, const Target &target);

template<>
bool is_native_xtensa_vector<int16_t>(const Type &t, const Target &target);

template<>
bool is_native_xtensa_vector<uint16_t>(const Type &t, const Target &target);

template<>
bool is_native_xtensa_vector<int32_t>(const Type &t, const Target &target);

template<>
bool is_native_xtensa_vector<int64_t>(const Type &t, const Target &target);

template<>
bool is_native_xtensa_vector<uint32_t>(const Type &t, const Target &target);

template<>
bool is_native_xtensa_vector<float16_t>(const Type &t, const Target &target);

template<>
bool is_native_xtensa_vector<float>(const Type &t, const Target &target);

bool is_native_vector_type(const Type &t, const Target &target);
bool is_double_native_vector_type(const Type &t, const Target &target);

Type get_native_xtensa_vector(const Type &t, const Target &target);

std::string suffix_for_type(Type t);

Stmt match_xtensa_patterns(const Stmt &s, const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif
