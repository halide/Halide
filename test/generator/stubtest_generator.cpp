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

    Output<Func> f{"f", 3};  // require a 3-dimensional Func but leave Type(s) unspecified
    Output<Func[]> g{ "g", Int(16), 2};   // leave ArraySize unspecified

    void generate() {
        // Gratuitous intermediate for the purpose of exercising
        // ScheduleParam<LoopLevel>
        intermediate(x, y, c) = input[0](x, y, c) * float_arg;

        f(x, y, c) = Tuple(
                intermediate(x, y, c),
                intermediate(x, y, c) + int_arg[0]);

        g.resize(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            g[i](x, y) = cast<int16_t>(input[i](x, y, 0) + int_arg[i]);
        }
    }

    void schedule() {
        if (intermediate_level.defined()) {
            intermediate.compute_at(intermediate_level);
        } else {
            intermediate.compute_at(f, x);
        }
        if (vectorize) intermediate.vectorize(x, natural_vector_size<float>());
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};

    Func intermediate{"intermediate"};
};

}  // namespace

namespace StubNS1 {
namespace StubNS2 {

// must forward-declare the name we want for the stub, inside the proper namespace(s).
// None of the namespace(s) may be anonymous (if you do, failures will occur at Halide
// compilation time).
class StubTest;


}  // namespace StubNS2
}  // namespace StubNS1

namespace {

// If the fully-qualified stub name specified for third argument hasn't been declared
// properly, a compile error will result. The fully-qualified name *must* have at least one
// namespace (i.e., a name at global scope is not acceptable).
HALIDE_REGISTER_GENERATOR(StubTest, "stubtest", StubNS1::StubNS2::StubTest)

}  // namespace
