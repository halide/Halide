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
        if (f.rowwise) {
            codec = rowwise_codec();
        } else {
            int ports = f.scheme.signature({{"values", Float(32), 2}}).outputs.size();  // e.g. planar: codes, scale
            codec = ApproximationCodec::make_codec(f.scheme, Float(32), f.rows > 1 ? 2 : 1, {"x", "y", ports > 1 ? std::vector<std::string>{} : std::vector<std::string>{"blocks"}});
        }
        if (quantize) {
            codec.adopt_encoder(*this);
        } else {
            codec.adopt_decoder(*this);
        }
    }

    void generate() {
        if (quantize && codec.encoded.size() > 1) {
            return;             // several outputs share the quantizer: make_codec's default schedule
        } else if (quantize) {  // per block, vectorized as mul_mat's activation encoder
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

    // make_codec's codec, on rows x [K, R] of a rowwise format, which reduces
    // over their K / QK blocks: x's extent, so x comes first.
    ApproximationCodec::Codec rowwise_codec() const {
        ApproximationCodec::Codec c;
        c.values = ImageParam(Float(32), 2, "x");
        ggml::Format f = ggml::format(type, false, scaled, false, c.values.dim(0).extent() / ggml::QK);
        Var k("v0"), i("v1");
        c.decoded = Func("y");
        c.decoded(k, i) = c.values(k, i);
        c.result = Func(c.values).approximate_by(f.scheme, {c.decoded});
        for (Func g : c.result.intermediates) {
            if (g.has_update_definition()) g.compute_root();
        }
        for (size_t p = 0; p < c.result.encoded.size(); p++) {
            const Func &e = c.result.encoded[p];
            c.encoded_names.push_back(ApproximationCodec::Detail::sanitize_port_name(c.result.encoded_ports[p].name, p));
            c.encoded.emplace_back(c.encoded_names.back());
            c.encoded.back()(_) = e(_);
        }
        c.encoded_inputs = Pipeline({c.decoded}).sever(c.result.encoded, c.encoded_names).online_inputs;
        c.decoded_name = "y";
        return c;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Codec, codec)
