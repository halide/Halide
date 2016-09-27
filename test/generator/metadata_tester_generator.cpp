#include "Halide.h"

namespace {

enum class SomeEnum { Foo,
                      Bar };

class MetadataTester : public Halide::Generator<MetadataTester> {
public:
    // Default values for all of these are deliberately wrong:
    GeneratorParam<Type> input_type{ "input_type", Int(16) };    // must be overridden to UInt(8)
    GeneratorParam<int> input_dim{ "input_dim", 2 };             // must be overridden to 3
    GeneratorParam<Type> output_type{ "output_type", Int(16) };  // must be overridden to Float(32)
    GeneratorParam<int> output_dim{ "output_dim", 2 };           // must be overridden to 3 
    GeneratorParam<int> array_count{ "array_count", 32 };        // must be overridden to 2

    Input<Func> input{ "input", input_type, input_dim };
    Input<bool> b{ "b", true };
    Input<int8_t> i8{ "i8", 8, -8, 127 }; 
    Input<int16_t> i16{ "i16", 16, -16, 127 };
    Input<int32_t> i32{ "i32", 32, -32, 127 };
    Input<int64_t> i64{ "i64", 64, -64, 127 };
    Input<uint8_t> u8{ "u8", 80, 8, 255 };
    Input<uint16_t> u16{ "u16", 160, 16, 2550 };
    Input<uint32_t> u32{ "u32", 320, 32, 2550 };
    Input<uint64_t> u64{ "u64", 640, 64, 2550 };
    Input<float> f32{ "f32", 32.1234f, -3200.1234f, 3200.1234f };
    Input<double> f64{ "f64", 64.25f, -6400.25f, 6400.25f };
    Input<void *> h{ "h", nullptr };

    Input<Func[]> array_input{ array_count, "array_input", input_type, input_dim };
    Input<Func[2]> array2_input{ "array2_input", input_type, input_dim };
    Input<int8_t[]> array_i8{ array_count, "array_i8" };
    Input<int8_t[2]> array2_i8{ "array2_i8" };
    Input<int16_t[]> array_i16{ array_count, "array_i16", 16 };
    Input<int16_t[2]> array2_i16{ "array2_i16", 16 };
    Input<int32_t[]> array_i32{ array_count, "array_i32", 32, -32, 127 };
    Input<int32_t[2]> array2_i32{ "array2_i32", 32, -32, 127 };
    Input<void *[]> array_h{ array_count, "array_h", nullptr };
    // array count of 0 means there are no inputs: for AOT, doesn't affect C call signature
    // (Note that we can't use Func[0] for this, as some compilers don't properly distinguish
    // between T[] and T[0].)
    Input<Func[]> empty_inputs{ 0, "empty_inputs", Float(32), 3 };

    Output<Func> output{ "output", {output_type, Float(32)}, output_dim };
    Output<float> output_scalar{ "output_scalar" };
    Output<Func[]> array_outputs{ array_count, "array_outputs", Float(32), 3 };
    Output<Func[2]> array_outputs2{ "array_outputs2", Float(32), 3 };
    Output<float[2]> array_outputs3{ "array_outputs3" };
    // array count of 0 means there are no outputs: for AOT, doesn't affect C call signature.
    // (Note that we can't use Func[0] for this, as some compilers don't properly distinguish
    // between T[] and T[0].)
    Output<Func[]> empty_outputs{ 0, "empty_outputs", Float(32), 3 };

    void generate() {
        Var x, y, c;

        // These should both be zero; they are here to exercise the operator[] overloads
        Expr zero1 = array_input[1](x, y, c) - array_input[0](x, y, c);
        Expr zero2 = array_i32[1] - array_i32[0];

        Func f1, f2;
        f1(x, y, c) = cast(output_type, input(x, y, c) + zero1 + zero2);
        f2(x, y, c) = cast<float>(f1(x, y, c) + 1);

        output(x, y, c) = Tuple(f1(x, y, c), f2(x, y, c));
        output_scalar() = 1234.25f;
        for (size_t i = 0; i < array_outputs.size(); ++i) {
            array_outputs[i](x, y, c) = (i + 1) * 1.5f;
            array_outputs2[i](x, y, c) = (i + 1) * 1.5f;
            array_outputs3[i]() = 42.f;
        }
    }

    void schedule() {
        // empty
    }
};

Halide::RegisterGenerator<MetadataTester> register_MetadataTester{ "metadata_tester" };

}  // namespace
