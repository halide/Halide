// One codec generator for every scheme: the scheme's round trip on a row of
// floats (on rows, for layouts with several per record), severed at the
// encoded blocks; `quantize` picks the half to adopt (quantize: x -> blocks,
// dequantize: blocks -> y); `scaled`: its scaled codes (schemes.h), as kernels
// may pick, for the test that they read and write the same bytes.
#include "halide_approximation_codec.h"
#include "kernels/schedule.h"
#include "schemes/schemes.h"

namespace {

using namespace Halide;

class Codec : public Generator<Codec> {
public:
    GeneratorParam<std::string> type{"type", "q4_0"};
    GeneratorParam<bool> quantize{"quantize", true}, scaled{"scaled", false};

    void configure() {
        ggml::Format f = ggml::format(type, false, scaled);
        codec = ApproximationCodec::make_codec(f.scheme, Float(32), f.rows > 1 ? 2 : 1, {"x", "y", {"blocks"}});
        if (quantize) {
            codec.adopt_encoder(*this);
        } else {
            codec.adopt_decoder(*this);
        }
    }

    void generate() {
        if (quantize) {  // per block, vectorized as mul_mat's activation encoder
            Func e = codec.result.encoded[0];
            e.compute_at(codec.encoded[0], codec.encoded[0].args()[0]);
            ggml::encoder(e, {codec.result});
        } else {  // whole blocks: element index = vector lane
            codec.decoded.output_buffer().dim(0).set_min(0);
            Var k = codec.decoded.args()[0], b("b");
            codec.decoded.split(k, b, k, ggml::format(type).block, TailStrategy::RoundUp).vectorize(k);
        }
    }

private:
    ApproximationCodec::Codec codec;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Codec, codec)
