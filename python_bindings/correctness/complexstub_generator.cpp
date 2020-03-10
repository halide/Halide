#include "Halide.h"

namespace {

template<typename Type, int size = 4, int dim = 1>
Halide::Buffer<Type> make_image(int extra) {
    Halide::Buffer<Type> im(size, size, dim);
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            for (int c = 0; c < dim; c++) {
                im(x, y, c) = static_cast<Type>(x + y + c + extra);
            }
        }
    }
    return im;
}

class ComplexStub : public Halide::Generator<ComplexStub> {
public:
    GeneratorParam<Type> untyped_buffer_output_type{"untyped_buffer_output_type", Float(32)};
    GeneratorParam<bool> vectorize{"vectorize", true};
    GeneratorParam<LoopLevel> intermediate_level{"intermediate_level", LoopLevel::root()};

    Input<Buffer<uint8_t>> typed_buffer_input{"typed_buffer_input", 3};
    Input<Buffer<>> untyped_buffer_input{"untyped_buffer_input"};
    Input<Func> simple_input{"simple_input", 3};  // require a 3-dimensional Func but leave Type unspecified
    Input<Func[]> array_input{"array_input", 3};  // require a 3-dimensional Func but leave Type and ArraySize unspecified
    // Note that Input<Func> does not (yet) support Tuples
    Input<float> float_arg{"float_arg", 1.0f, 0.0f, 100.0f};
    Input<int32_t[]> int_arg{"int_arg", 1};  // leave ArraySize unspecified

    Output<Func> simple_output{"simple_output", Float(32), 3};
    Output<Func> tuple_output{"tuple_output", 3};             // require a 3-dimensional Func but leave Type(s) unspecified
    Output<Func[]> array_output{"array_output", Int(16), 2};  // leave ArraySize unspecified
    Output<Buffer<float>> typed_buffer_output{"typed_buffer_output"};
    Output<Buffer<>> untyped_buffer_output{"untyped_buffer_output"};
    Output<Buffer<uint8_t>> static_compiled_buffer_output{"static_compiled_buffer_output", 3};

    void generate() {
        simple_output(x, y, c) = cast<float>(simple_input(x, y, c));
        typed_buffer_output(x, y, c) = cast<float>(typed_buffer_input(x, y, c));
        // Note that if we are being invoked via a Stub, "untyped_buffer_output.type()" will
        // assert-fail, because there is no type constraint set: the type
        // will end up as whatever we infer from the values put into it. We'll use an
        // explicit GeneratorParam to allow us to set it.
        untyped_buffer_output(x, y, c) = cast(untyped_buffer_output_type, untyped_buffer_input(x, y, c));

        // Gratuitous intermediate for the purpose of exercising
        // GeneratorParam<LoopLevel>
        intermediate(x, y, c) = simple_input(x, y, c) * float_arg;

        tuple_output(x, y, c) = Tuple(
            intermediate(x, y, c),
            intermediate(x, y, c) + int_arg[0]);

        array_output.resize(array_input.size());
        for (size_t i = 0; i < array_input.size(); ++i) {
            array_output[i](x, y) = cast<int16_t>(array_input[i](x, y, 0) + int_arg[i]);
        }

        // This should be compiled into the Generator product itself,
        // and not produce another input for the Stub or AOT filter.
        Buffer<uint8_t> static_compiled_buffer = make_image<uint8_t>(42);
        static_compiled_buffer_output = static_compiled_buffer;
    }

    void schedule() {
        intermediate.compute_at(intermediate_level);
        intermediate.specialize(vectorize).vectorize(x, natural_vector_size<float>());
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};

    Func intermediate{"intermediate"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ComplexStub, complexstub)
