#include "Halide.h"

namespace {

enum class SomeEnum { Foo,
                      Bar };

class MetadataTester : public Halide::Generator<MetadataTester> {
public:
    GeneratorParam<Type> input_type{ "input_type", UInt(8) };
    GeneratorParam<Type> output_type{ "output_type", Float(32) };

    ImageParam input{ UInt(8), 3, "input" };
    Param<bool> b{ "b", true };
    Param<int8_t> i8{ "i8", 8, -8, 127 };
    Param<int16_t> i16{ "i16", 16, -16, 127 };
    Param<int32_t> i32{ "i32", 32, -32, 127 };
    Param<int64_t> i64{ "i64", 64, -64, 127 };
    Param<uint8_t> u8{ "u8", 80, 8, 255 };
    Param<uint16_t> u16{ "u16", 160, 16, 2550 };
    Param<uint32_t> u32{ "u32", 320, 32, 2550 };
    Param<uint64_t> u64{ "u64", 640, 64, 2550 };
    Param<float> f32{ "f32", 32.1234f, -3200.1234f, 3200.1234f };
    Param<double> f64{ "f64", 64.25f, -6400.25f, 6400.25f };
    Param<void *> h{ "h", nullptr };

    Func build() {
        input = ImageParam(input_type, input.dimensions(), input.name());

        Var x, y, c;

        Func f1, f2;
        f1(x, y, c) = cast(output_type, input(x, y, c));
        f2(x, y, c) = cast<float>(f1(x, y, c) + 1);

        Func output("output");
        output(x, y, c) = Tuple(f1(x, y, c), f2(x, y, c));
        return output;
    }
};

Halide::RegisterGenerator<MetadataTester> register_MetadataTester{ "metadata_tester" };

}  // namespace
