#include "Halide.h"

namespace {

class Configure : public Halide::Generator<Configure> {
public:
    GeneratorParam<int> num_extra_buffer_inputs{"num_extra_buffer_inputs", 3};

    Input<Buffer<>> input{"input"};
    Input<int> bias{"bias"};

    Output<Buffer<>> output{"output"};

    void configure() {
        configure_calls++;

        // It's fine to examine GeneratorParams in the configure() method.
        assert(num_extra_buffer_inputs == 3);

        // Pointers returned by add_input() are managed by the Generator;
        // user code must not free them. We can stash them in member variables
        // as-is or in containers, like so:
        for (int i = 0; i < num_extra_buffer_inputs; ++i) {
            auto *extra = add_input<Buffer<>>("extra_" + std::to_string(i), UInt(8), 2);
            extra_buffer_inputs.push_back(extra);
        }

        typed_extra_buffer_input = add_input<Buffer<int16_t, 2>>("typed_extra_buffer_input");

        extra_func_input = add_input<Func>("extra_func_input", UInt(16), 3);

        extra_scalar_input = add_input<int>("extra_scalar_input");

        extra_dynamic_scalar_input = add_input<Expr>("extra_dynamic_scalar_input", Int(8));

        extra_buffer_output = add_output<Buffer<>>("extra_buffer_output", Float(32), 3);

        extra_func_output = add_output<Func>("extra_func_output", Float(64), 2);

        // This is ok: you can't *examine* an Input or Output here, but you can call
        // set_type() iff the type is unspecified. (This allows you to base the type on,
        // e.g., the value in get_target(), or the value of any GeneratorParam.)
        input.set_type(Int(32));
        output.set_type(Int(32));

        // Ditto for set_dimensions.
        input.set_dimensions(3);
        output.set_dimensions(3);

        // Will fail: it is not legal to call set_type on an Input or Output that
        // already has a type specified.
        // bias.set_type(Int(32));

        // Will fail: it is not legal to examine Inputs in the configure() method
        // assert(input.dimensions() == 3);

        // Will fail: it is not legal to examine Inputs in the configure() method
        // Expr b = bias;
        // assert(b.defined());

        // Will fail: it is not legal to examine Outputs in the configure() method
        // Func o = output;
        // assert(output.defined());
    }

    void generate() {
        assert(configure_calls == 1);

        // Will fail: it is not legal to call set_type(), etc from anywhere but configure().
        // input.set_type(Int(32));
        // input.set_dimensions(3);

        // Attempting to call add_input() outside of the configure method will fail.
        // auto *this_will_fail = add_input<Buffer<>>("untyped_uint8", UInt(8), 2);

        assert((*extra_dynamic_scalar_input).type() == Int(8));

        Var x, y, c;

        Expr extra_sum = cast<int>(0);
        for (int i = 0; i < num_extra_buffer_inputs; ++i) {
            extra_sum += cast<int>((*extra_buffer_inputs[i])(x, y));
        }
        extra_sum += cast<int>((*typed_extra_buffer_input)(x, y));
        extra_sum += cast<int>((*extra_func_input)(x, y, c));
        extra_sum += *extra_scalar_input + *extra_dynamic_scalar_input;

        output(x, y, c) = input(x, y, c) + bias + extra_sum;

        (*extra_buffer_output)(x, y, c) = cast<float>(output(x, y, c));
        (*extra_func_output)(x, y) = cast<double>(output(x, y, 0));
    }

private:
    int configure_calls = 0;

    std::vector<Input<Buffer<>> *> extra_buffer_inputs;
    Input<Buffer<int16_t, 2>> *typed_extra_buffer_input;
    Input<Func> *extra_func_input;
    Input<int> *extra_scalar_input;
    Input<Expr> *extra_dynamic_scalar_input;

    Output<Buffer<>> *extra_buffer_output;
    Output<Func> *extra_func_output;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Configure, configure)
