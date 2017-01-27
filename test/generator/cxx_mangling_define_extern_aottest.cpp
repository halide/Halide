#include <stdio.h>

#include "HalideRuntime.h"
#include "HalideBuffer.h"

#include <assert.h>
#include <string.h>

#include "cxx_mangling_define_extern.h"
#include "cxx_mangling.h"

using namespace Halide::Runtime;

int32_t extract_value_global(int32_t *arg) {
    return *arg;
}

namespace HalideTest {

int32_t extract_value_ns(const int32_t *arg) {
    return *arg;
}

}

namespace HalideTest {

int cxx_mangling_1(void *ctx, halide_buffer_t *_input_buffer, int8_t _offset_i8, uint8_t _offset_u8, int16_t _offset_i16, uint16_t _offset_u16, int32_t _offset_i32, uint32_t _offset_u32, int64_t _offset_i64, uint64_t _offset_u64, bool _scale_direction, float _scale_f, double _scale_d, int32_t *_ptr, int32_t const *_const_ptr, void *_void_ptr, void const *_const_void_ptr, void *_string_ptr, void const *_const_string_ptr, halide_buffer_t *_f_buffer) {
    return cxx_mangling(_input_buffer, _offset_i8, _offset_u8, _offset_i16, _offset_u16, _offset_i32, _offset_u32, _offset_i64, _offset_u64, _scale_direction, _scale_f, _scale_d, _ptr, _const_ptr, _void_ptr, _const_void_ptr, _string_ptr, _const_string_ptr, nullptr, nullptr, nullptr, _f_buffer);
}

int cxx_mangling_2(void *ctx, halide_buffer_t *_input_buffer, int8_t _offset_i8, uint8_t _offset_u8, int16_t _offset_i16, uint16_t _offset_u16, int32_t _offset_i32, uint32_t _offset_u32, int64_t _offset_i64, uint64_t _offset_u64, bool _scale_direction, float _scale_f, double _scale_d, int32_t *_ptr, int32_t const *_const_ptr, void *_void_ptr, void const *_const_void_ptr, void *_string_ptr, void const *_const_string_ptr, halide_buffer_t *_f_buffer) {
    return cxx_mangling(_input_buffer, _offset_i8, _offset_u8, _offset_i16, _offset_u16, _offset_i32, _offset_u32, _offset_i64, _offset_u64, _scale_direction, _scale_f, _scale_d, _ptr, _const_ptr, _void_ptr, _const_void_ptr, _string_ptr, _const_string_ptr, nullptr, nullptr, nullptr, _f_buffer);
}

extern "C" int cxx_mangling_3(void *ctx, halide_buffer_t *_input_buffer, int8_t _offset_i8, uint8_t _offset_u8, int16_t _offset_i16, uint16_t _offset_u16, int32_t _offset_i32, uint32_t _offset_u32, int64_t _offset_i64, uint64_t _offset_u64, bool _scale_direction, float _scale_f, double _scale_d, int32_t *_ptr, int32_t const *_const_ptr, void *_void_ptr, void const *_const_void_ptr, void *_string_ptr, void const *_const_string_ptr, halide_buffer_t *_f_buffer) {
    return cxx_mangling(_input_buffer, _offset_i8, _offset_u8, _offset_i16, _offset_u16, _offset_i32, _offset_u32, _offset_i64, _offset_u64, _scale_direction, _scale_f, _scale_d, _ptr, _const_ptr, _void_ptr, _const_void_ptr, _string_ptr, _const_string_ptr, nullptr, nullptr, nullptr, _f_buffer);
}

};

int main(int argc, char **argv) {
    Buffer<uint8_t> input(100);

    for (int32_t i = 0; i < 100; i++) {
        input(i) = i;
    }

    Buffer<double> result_1(100), result_2(100), result_3(100);

    const void *user_context = nullptr;
    int ptr_arg = 42;
    int *int_ptr = &ptr_arg;
    const int *const_int_ptr = &ptr_arg;
    void *void_ptr = nullptr;
    const void *const_void_ptr = nullptr;
    std::string *string_ptr = nullptr;
    const std::string *const_string_ptr = nullptr;
    int r = HalideTest::cxx_mangling_define_extern(user_context, input, int_ptr, const_int_ptr,
                                                   void_ptr, const_void_ptr, string_ptr, const_string_ptr,
                                                   result_1, result_2, result_3);
    if (r != 0) {
        fprintf(stderr, "Failure!\n");
        exit(1);
    }

    printf("Success!\n");
    return 0;
}
