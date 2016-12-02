#include "Halide.h"

namespace {

enum class BagType { Paper, Plastic };

class StubTest : public Halide::Generator<StubTest> {
public:
    GeneratorParam<float> float_param{ "float_param", 3.1415926535f };
    GeneratorParam<BagType> bag_type{ "bag_type",
                                      BagType::Paper,
                                      { { "paper", BagType::Paper },
                                        { "plastic", BagType::Plastic } } };

    ScheduleParam<bool> vectorize{ "vectorize", true };
    ScheduleParam<LoopLevel> intermediate_level{ "intermediate_level", "undefined" };

    Input<Func[]> input{ "input", 3 };  // require a 3-dimensional Func but leave Type and ArraySize unspecified
    Input<float> float_arg{ "float_arg", 1.0f, 0.0f, 100.0f }; 
    Input<int32_t[]> int_arg{ "int_arg", 1 };  // leave ArraySize unspecified

    Output<Func> simple_output{ "simple_output", Float(32), 3};
    Output<Func> tuple_output{"tuple_output", 3};  // require a 3-dimensional Func but leave Type(s) unspecified
    Output<Func[]> array_output{ "array_output", Int(16), 2};   // leave ArraySize unspecified

    void generate() {
        simple_output(x, y, c) = cast<float>(input[0](x, y, c));

        // Gratuitous intermediate for the purpose of exercising
        // ScheduleParam<LoopLevel>
        intermediate(x, y, c) = input[0](x, y, c) * float_arg;

        tuple_output(x, y, c) = Tuple(
                intermediate(x, y, c),
                intermediate(x, y, c) + int_arg[0]);

        array_output.resize(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            array_output[i](x, y) = cast<int16_t>(input[i](x, y, 0) + int_arg[i]);
        }
    }

    void schedule() {
        if (intermediate_level.defined()) {
            intermediate.compute_at(intermediate_level);
        } else {
            intermediate.compute_at(tuple_output, x);
        }
        if (vectorize) {
            intermediate.vectorize(x, natural_vector_size<float>());
        }

        if (input.has_buffer()) {
            for (size_t i = 0; i < input.size(); ++i) {
                input.set_stride_constraint(i, 0, 1);
            }
        }
        if (simple_output.has_buffer()) {
            simple_output.set_stride_constraint(0, 1);
        }
        if (tuple_output.has_buffer()) {
            tuple_output.set_stride_constraint(0, 1);
        }
        if (array_output.has_buffer()) {
            for (size_t i = 0; i < array_output.size(); ++i) {
                array_output.set_stride_constraint(i, 0, 1);
            }
        }
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};

    Func intermediate{"intermediate"};
};

HALIDE_REGISTER_GENERATOR(StubTest, "StubNS1::StubNS2::StubTest")

}  // namespace

