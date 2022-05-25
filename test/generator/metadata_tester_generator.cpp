#include "Halide.h"

namespace {

using Halide::float16_t;

enum class SomeEnum { Foo,
                      Bar };

class MetadataTester : public Halide::Generator<MetadataTester> {
public:
    Input<Func> input{"input"};  // must be overridden to {UInt(8), 3}
    Input<Buffer<uint8_t, 3>> typed_input_buffer{"typed_input_buffer"};
    Input<Buffer<void, 3>> dim_only_input_buffer{"dim_only_input_buffer"};  // must be overridden to type=UInt(8)
    Input<Buffer<>> untyped_input_buffer{"untyped_input_buffer"};           // must be overridden to {UInt(8), 3}
    Input<int32_t> no_default_value{"no_default_value"};
    Input<bool> b{"b", true};
    Input<int8_t> i8{"i8", 8, -8, 127};
    Input<int16_t> i16{"i16", 16, -16, 127};
    Input<int32_t> i32{"i32", 32, -32, 127};
    Input<int64_t> i64{"i64", 64, -64, 127};
    Input<uint8_t> u8{"u8", 80, 8, 255};
    Input<uint16_t> u16{"u16", 160, 16, 2550};
    Input<uint32_t> u32{"u32", 320, 32, 2550};
    Input<uint64_t> u64{"u64", 640, 64, 2550};
    Input<float> f32{"f32", 32.1234f, -3200.1234f, 3200.1234f};
    Input<double> f64{"f64", 64.25f, -6400.25f, 6400.25f};
    Input<void *> h{"h", nullptr};
    Input<Func> input_not_nod{"input_not_nod"};            // must be overridden to type=uint8 dim=3
    Input<Func> input_nod{"input_nod", UInt(8)};           // must be overridden to type=uint8 dim=3
    Input<Func> input_not{"input_not", 3};                 // must be overridden to type=uint8
    Input<Func[]> array_input{"array_input", UInt(8), 3};  // must be overridden to size=2
    Input<Func[2]> array2_input{"array2_input", UInt(8), 3};
    Input<int8_t[]> array_i8{"array_i8"};  // must be overridden to size=2
    Input<int8_t[2]> array2_i8{"array2_i8"};
    Input<int16_t[]> array_i16{"array_i16", 16};  // must be overridden to size=2
    Input<int16_t[2]> array2_i16{"array2_i16", 16};
    Input<int32_t[]> array_i32{"array_i32", 32, -32, 127};  // must be overridden to size=2
    Input<int32_t[2]> array2_i32{"array2_i32", 32, -32, 127};
    Input<void *[]> array_h { "array_h", nullptr };  // must be overridden to size=2

    Input<Buffer<float, 3>[2]> buffer_array_input1 { "buffer_array_input1" };
    Input<Buffer<float>[2]> buffer_array_input2 { "buffer_array_input2" };    // buffer_array_input2.dim must be set
    Input<Buffer<void, 3>[2]> buffer_array_input3 { "buffer_array_input3" };  // buffer_array_input2.type must be set
    Input<Buffer<>[2]> buffer_array_input4 { "buffer_array_input4" };         // dim and type must be set
    // .size must be specified for all of these
    Input<Buffer<float, 3>[]> buffer_array_input5 { "buffer_array_input5" };
    Input<Buffer<float>[]> buffer_array_input6 { "buffer_array_input6" };    // buffer_array_input2.dim must be set
    Input<Buffer<void, 3>[]> buffer_array_input7 { "buffer_array_input7" };  // buffer_array_input2.type must be set
    Input<Buffer<>[]> buffer_array_input8 { "buffer_array_input8" };         // dim and type must be set

    Input<Buffer<float16_t, 1>> buffer_f16_typed{"buffer_f16_typed"};
    Input<Buffer<void, 1>> buffer_f16_untyped{"buffer_f16_untyped"};

    Input<Expr> untyped_scalar_input{"untyped_scalar_input"};  // untyped_scalar_input.type must be set to uint8

    Output<Func> output{"output"};  // must be overridden to {{Float(32), Float(32)}, 3}
    Output<Buffer<float, 3>> typed_output_buffer{"typed_output_buffer"};
    Output<Buffer<float>> type_only_output_buffer{"type_only_output_buffer"};  // untyped outputs can have type and/or dimensions inferred
    Output<Buffer<void, 3>> dim_only_output_buffer{"dim_only_output_buffer"};  // untyped outputs can have type and/or dimensions inferred
    Output<Buffer<>> untyped_output_buffer{"untyped_output_buffer"};           // untyped outputs can have type and/or dimensions inferred
    Output<Buffer<void, 3>> tupled_output_buffer{"tupled_output_buffer", {Float(32), Int(32)}};
    Output<float> output_scalar{"output_scalar"};
    Output<Func[]> array_outputs{"array_outputs", Float(32), 3};  // must be overridden to size=2
    Output<Func[2]> array_outputs2{"array_outputs2", {Float(32), Float(32)}, 3};
    Output<float[2]> array_outputs3{"array_outputs3"};

    Output<Buffer<float, 3>[2]> array_outputs4 { "array_outputs4" };
    Output<Buffer<float>[2]> array_outputs5 { "array_outputs5" };  // dimensions will be inferred by usage
    Output<Buffer<>[2]> array_outputs6 { "array_outputs6" };       // dimensions and type will be inferred by usage

    // .size must be specified for all of these
    Output<Buffer<float, 3>[]> array_outputs7 { "array_outputs7" };
    Output<Buffer<float>[]> array_outputs8 { "array_outputs8" };
    Output<Buffer<>[]> array_outputs9 { "array_outputs9" };

    // Output<void> untyped_scalar_output{"untyped_scalar_output"};  // untyped_scalar_output.type must be set

    void generate() {
        Var x("x"), y("y"), c("c");

        assert(buffer_f16_untyped.type() == Float(16));
        assert(untyped_scalar_input.type() == UInt(8));

        // These should all be zero; they are here to exercise the operator[] overloads
        Expr zero1 = array_input[1](x, y, c) - array_input[0](x, y, c);
        Expr zero2 = array_i32[1] - array_i32[0];

        Expr bzero1 = buffer_array_input1[1](x, y, c) - buffer_array_input1[0](x, y, c);
        Expr bzero2 = buffer_array_input2[1](x, y, c) - buffer_array_input2[0](x, y, c);
        Expr bzero3 = buffer_array_input3[1](x, y, c) - buffer_array_input3[0](x, y, c);
        Expr bzero4 = buffer_array_input4[1](x, y, c) - buffer_array_input4[0](x, y, c);
        Expr bzero5 = buffer_array_input5[1](x, y, c) - buffer_array_input5[0](x, y, c);
        Expr bzero6 = buffer_array_input6[1](x, y, c) - buffer_array_input6[0](x, y, c);
        Expr bzero7 = buffer_array_input7[1](x, y, c) - buffer_array_input7[0](x, y, c);
        Expr bzero8 = buffer_array_input8[1](x, y, c) - buffer_array_input8[0](x, y, c);

        Expr zero = zero1 + zero2 + bzero1 + bzero2 + bzero3 + bzero4 + bzero5 + bzero6 + bzero7 + bzero8;

        assert(output.types().size() == 2);
        Type output_type = output.types().at(0);

        Func f1("f1"), f2("f2");
        f1(x, y, c) = cast(output_type, input(x, y, c) + zero + untyped_scalar_input);
        f2(x, y, c) = cast<float>(f1(x, y, c) + 1);

        Func t1("t1");
        t1(x, y, c) = Tuple(f1(x, y, c), f2(x, y, c));

        output = t1;
        typed_output_buffer(x, y, c) = f1(x, y, c);
        type_only_output_buffer(x, y, c) = f1(x, y, c);
        dim_only_output_buffer(x, y, c) = f1(x, y, c);
        tupled_output_buffer(x, y, c) = Tuple(f2(x, y, c), cast<int32_t>(f2(x, y, c) + 1.5f));
        // verify that we can assign a Func to an Output<Buffer<>>
        untyped_output_buffer = f2;
        output_scalar() = 1234.25f;
        for (size_t i = 0; i < array_outputs.size(); ++i) {
            array_outputs[i](x, y, c) = (i + 1) * 1.5f;
            Func z1("z1");
            z1(x, y, c) = Tuple((i + 1) * 1.5f, 42.f);
            array_outputs2[i] = z1;
            array_outputs3[i]() = 42.f;

            array_outputs4[i](x, y, c) = cast<float>(x + y + c + (int)i);
            array_outputs5[i](x, y, c) = cast<float>(x + y + c + (int)i);
            array_outputs6[i](x, y, c) = cast<float>(x + y + c + (int)i);
            array_outputs7[i](x, y, c) = cast<float>(x + y + c + (int)i);
            array_outputs8[i](x, y, c) = cast<float>(x + y + c + (int)i);
            array_outputs9[i](x, y, c) = cast<float>(x + y + c + (int)i);

            // Verify compute_with works for Output<Func>
            array_outputs2[i].compute_with(array_outputs[i], x);
        }

        // Verify compute_with works for Output<Buffer>
        dim_only_output_buffer.compute_with(Func(typed_output_buffer), x);

        // Provide some bounds estimates for a Buffer input
        typed_input_buffer.set_estimate(Halide::_0, 0, 2592);
        typed_input_buffer.dim(1).set_estimate(42, 1968);

        // Provide some bounds estimates for a Func input
        input
            .set_estimate(Halide::_0, 10, 2592)
            .set_estimate(Halide::_1, 20, 1968)
            .set_estimate(Halide::_2, 0, 3);

        // Provide some scalar estimates.
        b.set_estimate(false);
        i8.set_estimate(3);
        f32.set_estimate(48.5f);

        array2_i8.set_estimate(0, 42);

        // Provide some bounds estimates for an Output<Func>.
        // Note that calling bound() implicitly calls estimate() as well.
        output
            .set_estimate(x, 10, 2592)
            .set_estimate(y, 20, 1968)
            .bound(c, 0, 3);

        // Provide partial bounds estimates for an Output<Buffer>
        typed_output_buffer.set_estimate(x, 10, 2592);
        typed_output_buffer.dim(1).set_estimate(20, 1968);

        // Note that calling set_bounds()/bound() implicitly calls set_estimate()/estimate() as well.
        type_only_output_buffer.dim(1).set_bounds(0, 32);
        type_only_output_buffer.bound(c, 0, 3);

        // Verify that we can call schedule methods on arrays-of-Buffer
        for (int i = 0; i < 2; i++) {
            array_outputs4[i].compute_root();
        }
    }

    void schedule() {
        // empty
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MetadataTester, metadata_tester)
