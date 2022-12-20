#include "Halide.h"

namespace {

template<typename Type, int size = 4, int dim = 1>
Halide::Buffer<Type, 3> make_image(int extra) {
    Halide::Buffer<Type, 3> im(size, size, dim);
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            for (int c = 0; c < dim; c++) {
                im(x, y, c) = static_cast<Type>(x + y + c + extra);
            }
        }
    }
    return im;
}

class ComplexCpp : public Halide::Generator<ComplexCpp> {
public:
    GeneratorParam<bool> vectorize{"vectorize", true};
    GeneratorParam<std::string> extra_input_name{"extra_input_name", ""};

    Input<Buffer<uint8_t, 3>> typed_buffer_input{"typed_buffer_input"};
    Input<Buffer<void, 3>> untyped_buffer_input{"untyped_buffer_input"};
    Input<Buffer<void, 3>> simple_input{"simple_input"};
    Input<float> float_arg{"float_arg", 1.0f, 0.0f, 100.0f};
    Input<int32_t> int_arg{"int_arg", 1};

    Output<Buffer<float, 3>> simple_output{"simple_output"};
    Output<Buffer<void, 3>> tuple_output{"tuple_output"};
    Output<Buffer<float, 3>> typed_buffer_output{"typed_buffer_output"};
    Output<Buffer<void, -1>> untyped_buffer_output{"untyped_buffer_output"};
    Output<Buffer<uint8_t, 3>> static_compiled_buffer_output{"static_compiled_buffer_output"};
    Output<float> scalar_output{"scalar_output"};

    void configure() {
        // Pointers returned by add_input() are managed by the Generator;
        // user code must not free them. We can stash them in member variables
        // as-is or in containers, like so:
        std::string n = extra_input_name;
        if (!n.empty()) {
            extra_input = add_input<Buffer<uint16_t, 3>>(n);
        }
        extra_output = add_output<Buffer<double, 2>>("extra_output");
    }

    void generate() {
        simple_output(x, y, c) = cast<float>(simple_input(x, y, c));
        typed_buffer_output(x, y, c) = cast<float>(typed_buffer_input(x, y, c));
        untyped_buffer_output(x, y, c) = cast(untyped_buffer_output.type(), untyped_buffer_input(x, y, c));

        Func intermediate("intermediate");
        intermediate(x, y, c) = simple_input(x, y, c) * float_arg;

        tuple_output(x, y, c) = Tuple(
            intermediate(x, y, c),
            intermediate(x, y, c) + int_arg);

        // This should be compiled into the Generator product itself,
        // and not produce another input for the Stub or AOT filter.
        Buffer<uint8_t, 3> static_compiled_buffer = make_image<uint8_t>(42);
        static_compiled_buffer_output = static_compiled_buffer;
        if (extra_input) {
            (*extra_output)(x, y) = cast<double>((*extra_input)(x, y, 0) + 1);
        } else {
            (*extra_output)(x, y) = cast<double>(0);
        }

        scalar_output() = float_arg + int_arg;

        intermediate.compute_at(tuple_output, y);
        intermediate.specialize(vectorize).vectorize(x, natural_vector_size<float>());
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};

    Input<Buffer<uint16_t, 3>> *extra_input = nullptr;
    Output<Buffer<double, 2>> *extra_output = nullptr;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ComplexCpp, complexcpp)
