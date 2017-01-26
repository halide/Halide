#include "Halide.h"

namespace {

enum class SomeEnum { Foo,
                      Bar };

class MetadataTester : public Halide::Generator<MetadataTester> {
public:
    Input<Func> input{ "input", Int(16), 2 };  // must be overridden to {UInt(8), 3}
    Input<Buffer<uint8_t>> typed_input_buffer{ "typed_input_buffer", 3 };
    Input<Buffer<>> type_only_input_buffer{ "type_only_input_buffer", UInt(8) };  // must be overridden to dim=3
    Input<Buffer<>> dim_only_input_buffer{ "dim_only_input_buffer", 3 };  // must be overridden to type=UInt(8)
    Input<Buffer<>> untyped_input_buffer{ "untyped_input_buffer" };  // must be overridden to {UInt(8), 3}
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
    
    Input<Func> input_not_nod{ "input_not_nod" };  // must be overridden to type=uint8 dim=3
    Input<Func> input_nod{ "input_nod", UInt(8) }; // must be overridden to type=uint8 dim=3
    Input<Func> input_not{ "input_not", 3 };       // must be overridden to type=uint8

    Input<Func[]> array_input{ "array_input", UInt(8), 3 };  // must be overridden to size=2
    Input<Func[2]> array2_input{ "array2_input", UInt(8), 3 };
    Input<int8_t[]> array_i8{ "array_i8" };  // must be overridden to size=2
    Input<int8_t[2]> array2_i8{ "array2_i8" };
    Input<int16_t[]> array_i16{ "array_i16", 16 };  // must be overridden to size=2
    Input<int16_t[2]> array2_i16{ "array2_i16", 16 };
    Input<int32_t[]> array_i32{ "array_i32", 32, -32, 127 };  // must be overridden to size=2
    Input<int32_t[2]> array2_i32{ "array2_i32", 32, -32, 127 };
    Input<void *[]> array_h{ "array_h", nullptr };  // must be overridden to size=2

    Output<Func> output{ "output", {Int(16), UInt(8)}, 2 };  // must be overridden to {{Float(32), Float(32)}, 3}
    Output<Buffer<float>> typed_output_buffer{ "typed_output_buffer", 3 };
    Output<Buffer<float>> type_only_output_buffer{ "type_only_output_buffer" };  // untyped outputs can have type and/or dimensions inferred
    Output<Buffer<>> dim_only_output_buffer{ "dim_only_output_buffer", 3 };  // untyped outputs can have type and/or dimensions inferred
    Output<Buffer<>> untyped_output_buffer{ "untyped_output_buffer" };  // untyped outputs can have type and/or dimensions inferred
    Output<float> output_scalar{ "output_scalar" };
    Output<Func[]> array_outputs{ "array_outputs", Float(32), 3 };  // must be overridden to size=2
    Output<Func[2]> array_outputs2{ "array_outputs2", Float(32), 3 };
    Output<float[2]> array_outputs3{ "array_outputs3" };

    void generate() {
        Var x, y, c;

        // These should both be zero; they are here to exercise the operator[] overloads
        Expr zero1 = array_input[1](x, y, c) - array_input[0](x, y, c);
        Expr zero2 = array_i32[1] - array_i32[0];

        assert(output.types().size() == 2);
        Type output_type = output.types().at(0);

        Func f1, f2;
        f1(x, y, c) = cast(output_type, input(x, y, c) + zero1 + zero2);
        f2(x, y, c) = cast<float>(f1(x, y, c) + 1);

        output(x, y, c) = Tuple(f1(x, y, c), f2(x, y, c));
        typed_output_buffer(x, y, c) = f1(x, y, c);
        type_only_output_buffer(x, y, c) = f1(x, y, c);
        dim_only_output_buffer(x, y, c) = f1(x, y, c);
        untyped_output_buffer(x, y, c) = f2(x, y, c);
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

}  // namespace

HALIDE_REGISTER_GENERATOR(MetadataTester, "metadata_tester")
