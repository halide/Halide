#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "all_type_names.h"

using namespace Halide::Runtime;

const int kSize = 32;

int main(int argc, char **argv) {
    int32_t result;

    Buffer<int8_t, 1> input_i8(kSize);
    Buffer<int16_t, 1> input_i16(kSize);
    Buffer<int32_t, 1> input_i32(kSize);
    Buffer<int64_t, 1> input_i64(kSize);
    Buffer<uint8_t, 1> input_u8(kSize);
    Buffer<uint16_t, 1> input_u16(kSize);
    Buffer<uint32_t, 1> input_u32(kSize);
    Buffer<uint64_t, 1> input_u64(kSize);
    Buffer<uint16_t, 1> input_f16(kSize);
    Buffer<float, 1> input_f32(kSize);
    Buffer<double, 1> input_f64(kSize);
    Buffer<uint16_t, 1> input_bf16(kSize);
    Buffer<double, 1> output(kSize);

    input_i8.fill(1);
    input_i16.fill(1);
    input_i32.fill(1);
    input_i64.fill(1);
    input_u8.fill(1);
    input_u16.fill(1);
    input_u32.fill(1);
    input_u64.fill(1);
    // Start with a u16 Buffer so it can be initialized then convert to float16.
    input_f16.fill(0x3C00);
    input_f16.raw_buffer()->type.code = halide_type_float;
    input_f32.fill(1.0f);
    input_f64.fill(1.0);
    // Start with a u16 Buffer so it can be initialized then convert to bfloat16.
    input_bf16.fill(0x3F80);
    input_bf16.raw_buffer()->type.code = halide_type_bfloat;

    result = all_type_names(input_i8, input_i16, input_i32, input_i64,
                            input_u8, input_u16, input_u32, input_u64,
                            input_f16, input_f32, input_f64, input_bf16,
                            output);
    assert(result == 0);
    output.for_each_element([=](int x) {
        assert(output(x) == 10.0);
    });

    printf("Success!\n");
    return 0;
}
