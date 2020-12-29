#include <cstdint>

#include "cxx_mangling.h"

// These are the define_extern functions referenced by cxx_mangling_define_extern_generator.cpp
namespace HalideTest {

int cxx_mangling_1(void *ctx, halide_buffer_t *_input_buffer, int8_t _offset_i8, uint8_t _offset_u8, int16_t _offset_i16, uint16_t _offset_u16, int32_t _offset_i32, uint32_t _offset_u32, int64_t _offset_i64, uint64_t _offset_u64, bool _scale_direction, float _scale_f, double _scale_d, int32_t *_ptr, int32_t const *_const_ptr, void *_void_ptr, void const *_const_void_ptr, void *_string_ptr, void const *_const_string_ptr, halide_buffer_t *_f_buffer) {
    return AnotherNamespace::cxx_mangling(_input_buffer, _offset_i8, _offset_u8, _offset_i16, _offset_u16, _offset_i32, _offset_u32, _offset_i64, _offset_u64, _scale_direction, _scale_f, _scale_d, _ptr, _const_ptr, _void_ptr, _const_void_ptr, _string_ptr, _const_string_ptr, nullptr, nullptr, nullptr, _f_buffer);
}

int cxx_mangling_2(void *ctx, halide_buffer_t *_input_buffer, int8_t _offset_i8, uint8_t _offset_u8, int16_t _offset_i16, uint16_t _offset_u16, int32_t _offset_i32, uint32_t _offset_u32, int64_t _offset_i64, uint64_t _offset_u64, bool _scale_direction, float _scale_f, double _scale_d, int32_t *_ptr, int32_t const *_const_ptr, void *_void_ptr, void const *_const_void_ptr, void *_string_ptr, void const *_const_string_ptr, halide_buffer_t *_f_buffer) {
    return AnotherNamespace::cxx_mangling(_input_buffer, _offset_i8, _offset_u8, _offset_i16, _offset_u16, _offset_i32, _offset_u32, _offset_i64, _offset_u64, _scale_direction, _scale_f, _scale_d, _ptr, _const_ptr, _void_ptr, _const_void_ptr, _string_ptr, _const_string_ptr, nullptr, nullptr, nullptr, _f_buffer);
}

extern "C" int cxx_mangling_3(void *ctx, halide_buffer_t *_input_buffer, int8_t _offset_i8, uint8_t _offset_u8, int16_t _offset_i16, uint16_t _offset_u16, int32_t _offset_i32, uint32_t _offset_u32, int64_t _offset_i64, uint64_t _offset_u64, bool _scale_direction, float _scale_f, double _scale_d, int32_t *_ptr, int32_t const *_const_ptr, void *_void_ptr, void const *_const_void_ptr, void *_string_ptr, void const *_const_string_ptr, halide_buffer_t *_f_buffer) {
    return AnotherNamespace::cxx_mangling(_input_buffer, _offset_i8, _offset_u8, _offset_i16, _offset_u16, _offset_i32, _offset_u32, _offset_i64, _offset_u64, _scale_direction, _scale_f, _scale_d, _ptr, _const_ptr, _void_ptr, _const_void_ptr, _string_ptr, _const_string_ptr, nullptr, nullptr, nullptr, _f_buffer);
}

};  // namespace HalideTest
