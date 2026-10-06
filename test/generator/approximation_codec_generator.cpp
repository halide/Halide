#include "Halide.h"
#include "halide_approximation_codec.h"

using namespace Halide;

namespace {

enum class Direction { Encode,
                       Decode };

// Symmetric per-block quantization of (within, block) to int8 codes and a
// scale per block.
struct BlockQuantizer {
    int block;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Var i("i"), b("b");
        RDom r(0, block);
        Func amax("amax"), scale("block_scale"), codes("block_codes");
        amax(b) = maximum(abs(in[0](r, b)));
        scale(b) = amax(b) / 127.0f;
        codes(i, b) = cast<int8_t>(round(in[0](i, b) / select(scale(b) == 0.0f, 1.0f, scale(b))));
        return {codes, scale};
    }

    std::vector<Func> decode(const std::vector<Func> &encoded) const {
        Var i("i"), b("b");
        Func out("dequantized");
        out(i, b) = cast<float>(encoded[0](i, b)) * encoded[1](b);
        return {out};
    }

    ApproximationSignature signature() const {
        return {{{"block", Float(32)}}, {{"codes", Int(8)}, {"scale", Float(32)}}};
    }
};

// One configure() body for both directions: make_codec() builds the round
// trip and severs it, and each direction adopts its half as its ports.
class ApproximationCodecGenerator : public Generator<ApproximationCodecGenerator> {
public:
    GeneratorParam<Direction> direction{"direction",
                                        Direction::Encode,
                                        {{"encode", Direction::Encode},
                                         {"decode", Direction::Decode}}};
    GeneratorParam<int> block_size{"block_size", 8};

    void configure() {
        // Store the codes offset to unsigned bytes. (Not float16 scales: the
        // C backend this test also builds does not support them everywhere.)
        Pointwise offset{"offset",
                         [](Expr x) { return cast<uint8_t>(cast<int>(x) + 128); },
                         [](Expr x) { return cast<int8_t>(cast<int>(x) - 128); }};
        Approximation scheme = Compose(BlockReshape{block_size}, BlockQuantizer{block_size},
                                       Parallel{{"codes", offset}, {"scale", Identity{}}});
        ApproximationCodec::Codec codec = ApproximationCodec::make_codec(scheme, Float(32), 1);
        if (direction == Direction::Encode) {
            codec.values.dim(0).set_min(0);
            codec.adopt_encoder(*this);
        } else {
            codec.decoded.output_buffer().dim(0).set_min(0);
            codec.adopt_decoder(*this);
        }
    }

    void generate() {
        // Nothing to do: configure() already built and wired up the pipeline.
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ApproximationCodecGenerator, approximation_codec)
