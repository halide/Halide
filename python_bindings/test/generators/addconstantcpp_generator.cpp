
#include "Halide.h"

using namespace Halide;

class AddConstantGenerator : public Halide::Generator<AddConstantGenerator> {
public:
    GeneratorParam<int> extra_int{"extra_int", 0};

    Input<bool> scalar_uint1{"scalar_uint1"};
    Input<uint8_t> scalar_uint8{"scalar_uint8"};
    Input<uint16_t> scalar_uint16{"scalar_uint16"};
    Input<uint32_t> scalar_uint32{"scalar_uint32"};
    Input<uint64_t> scalar_uint64{"scalar_uint64"};
    Input<int8_t> scalar_int8{"scalar_int8"};
    Input<int16_t> scalar_int16{"scalar_int16"};
    Input<int32_t> scalar_int32{"scalar_int32"};
    Input<int64_t> scalar_int64{"scalar_int64"};
    Input<float> scalar_float{"scalar_float"};
    Input<double> scalar_double{"scalar_double"};

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
        add_requirement(scalar_int32 != 0);  // error_args omitted for this case
        add_requirement(scalar_int32 > 0, "negative values are bad", scalar_int32);

        output_uint8(x) = input_uint8(x) + scalar_uint8;
        output_uint16(x) = input_uint16(x) + scalar_uint16;
        output_uint32(x) = input_uint32(x) + scalar_uint32;
        output_uint64(x) = input_uint64(x) + scalar_uint64;
        output_int8(x) = input_int8(x) + scalar_int8;
        output_int16(x) = input_int16(x) + scalar_int16;
        output_int32(x) = input_int32(x) + scalar_int32;
        output_int64(x) = input_int64(x) + scalar_int64;
        output_float(x) = input_float(x) + scalar_float;
        output_double(x) = input_double(x) + scalar_double;
        output_2d(x, y) = input_2d(x, y) + scalar_int8;
        output_3d(x, y, z) = input_3d(x, y, z) + scalar_int8 + extra_int;
    }

    void schedule() {
    }
};

HALIDE_REGISTER_GENERATOR(AddConstantGenerator, addconstantcpp)
HALIDE_REGISTER_GENERATOR_ALIAS(addconstantcpp_with_offset_42, addconstantcpp, {{"extra_int", "42"}})
HALIDE_REGISTER_GENERATOR_ALIAS(addconstantcpp_with_negative_offset, addconstantcpp, {{"extra_int", "-1"}})
