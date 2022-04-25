#include "Halide.h"

using namespace Halide;

class AddConstantGenerator : public Halide::Generator<AddConstantGenerator> {
public:
    Input<bool> constant_uint1{"constant_uint1"};
    Input<uint8_t> constant_uint8{"constant_uint8"};
    Input<uint16_t> constant_uint16{"constant_uint16"};
    Input<uint32_t> constant_uint32{"constant_uint32"};
    Input<uint64_t> constant_uint64{"constant_uint64"};
    Input<int8_t> constant_int8{"constant_int8"};
    Input<int16_t> constant_int16{"constant_int16"};
    Input<int32_t> constant_int32{"constant_int32"};
    Input<int64_t> constant_int64{"constant_int64"};
    Input<float> constant_float{"constant_float"};
    Input<double> constant_double{"constant_double"};

    Input<Buffer<uint8_t, 1>> input_uint8{"input_uint8"};
    Input<Buffer<uint16_t, 1>> input_uint16{"input_uint16"};
    Input<Buffer<uint32_t, 1>> input_uint32{"input_uint32"};
    Input<Buffer<uint64_t, 1>> input_uint64{"input_uint64"};
    Input<Buffer<int8_t, 1>> input_int8{"input_int8"};
    Input<Buffer<int16_t, 1>> input_int16{"input_int16"};
    Input<Buffer<int32_t, 1>> input_int32{"input_int32"};
    Input<Buffer<int64_t, 1>> input_int64{"input_int64"};
    Input<Buffer<float, 1>> input_float{"input_float"};
    Input<Buffer<double, 1>> input_double{"input_double"};
    Input<Buffer<int8_t, 2>> input_2d{"input_2d"};
    Input<Buffer<int8_t, 3>> input_3d{"input_3d"};

    Output<Buffer<uint8_t, 1>> output_uint8{"output_uint8"};
    Output<Buffer<uint16_t, 1>> output_uint16{"output_uint16"};
    Output<Buffer<uint32_t, 1>> output_uint32{"output_uint32"};
    Output<Buffer<uint64_t, 1>> output_uint64{"output_uint64"};
    Output<Buffer<int8_t, 1>> output_int8{"output_int8"};
    Output<Buffer<int16_t, 1>> output_int16{"output_int16"};
    Output<Buffer<int32_t, 1>> output_int32{"output_int32"};
    Output<Buffer<int64_t, 1>> output_int64{"output_int64"};
    Output<Buffer<float, 1>> output_float{"output_float"};
    Output<Buffer<double, 1>> output_double{"output_double"};
    Output<Buffer<int8_t, 2>> output_2d{"buffer_2d"};
    Output<Buffer<int8_t, 3>> output_3d{"buffer_3d"};

    Var x, y, z;

    void generate() {
        output_uint8(x) = input_uint8(x) + constant_uint8;
        output_uint16(x) = input_uint16(x) + constant_uint16;
        output_uint32(x) = input_uint32(x) + constant_uint32;
        output_uint64(x) = input_uint64(x) + constant_uint64;
        output_int8(x) = input_int8(x) + constant_int8;
        output_int16(x) = input_int16(x) + constant_int16;
        output_int32(x) = input_int32(x) + constant_int32;
        output_int64(x) = input_int64(x) + constant_int64;
        output_float(x) = input_float(x) + constant_float;
        output_double(x) = input_double(x) + constant_double;
        output_2d(x, y) = input_2d(x, y) + constant_int8;
        output_3d(x, y, z) = input_3d(x, y, z) + constant_int8;
    }

    void schedule() {
    }
};

HALIDE_REGISTER_GENERATOR(AddConstantGenerator, addconstant)
