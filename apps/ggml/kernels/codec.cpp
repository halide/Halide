// One codec generator for every scheme: builds the round trip of a row of
// floats, severs it at the encoded blocks, and adopts the half that
// `direction` asks for (quantize: x -> blocks, dequantize: blocks -> y).
#include "schemes/schemes.h"

namespace {

using namespace Halide;

class Codec : public Generator<Codec> {
public:
    GeneratorParam<std::string> type{"type", "q4_0"};
    GeneratorParam<bool> quantize{"quantize", true};

    void configure() {
        ImageParam x(Float(32), 1, "x");
        Var k("k");
        Func y("y");
        y(k) = x(k);
        ApproximationResult r = Func(x).approximate_by(ggml::scheme(type), {y});
        for (Func f : r.intermediates) {
            if (f.has_update_definition()) f.compute_root();
        }
        SeverResult split = Pipeline(y).sever(r.encoded, {"blocks"});
        if (quantize) {
            add_input(x);
            add_output(split.offline.outputs()[0]);
        } else {
            add_input(split.online_inputs[0]);
            values = add_output(y);
        }
    }

    void generate() {
        if (values) {  // whole blocks: element index = vector lane
            values->dim(0).set_min(0);
            Var k = values->args()[0], b("b");
            Func(*values).split(k, b, k, ggml::QK, TailStrategy::RoundUp).vectorize(k);
        }
    }

private:
    GeneratorOutput<Buffer<>> *values = nullptr;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Codec, codec)
