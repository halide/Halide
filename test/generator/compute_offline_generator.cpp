#include "Halide.h"

namespace {

enum class Direction { Quantize,
                       Dequantize };

// Builds a blockwise symmetric int8 quantize/dequantize round trip entirely in
// configure(), splits it with Pipeline::compute_offline(), and then adopts one
// half as this Generator's ports via add_input(const ImageParam &) and
// add_output(const Func &). Both directions share the same configure() body;
// generate() is an empty stub.
class ComputeOffline : public Halide::Generator<ComputeOffline> {
public:
    GeneratorParam<Direction> direction{"direction",
                                        Direction::Quantize,
                                        {{"quantize", Direction::Quantize},
                                         {"dequantize", Direction::Dequantize}}};
    GeneratorParam<int> block_size{"block_size", 8};

    void configure() {
        const int bs = block_size;

        Var k("k"), b("b");
        RDom r(0, bs, "r");

        ImageParam x(Float(32), 1, "x");

        Func amax("amax");
        amax(b) = 0.0f;
        amax(b) = max(amax(b), abs(x(b * bs + r)));

        Func scale("scale");
        scale(b) = amax(b) / 127.0f;

        Func q("q");
        Expr s = scale(k / bs);
        Expr inv = select(s != 0.0f, 1.0f / s, 0.0f);
        q(k) = cast<int8_t>(clamp(round(x(k) * inv), -127, 127));

        Func y("y");
        y(k) = cast<float>(q(k)) * scale(k / bs);

        // Name the severed Funcs' stand-in ImageParams, so the dequantize
        // direction's input ports have meaningful names.
        ComputeOfflineResult split =
            Pipeline({y}).compute_offline({q, scale}, {"q_in", "scale_in"});

        // Adopt one half as this Generator's ports. Typed ports check the
        // adopted ImageParam/Func's type and dimensionality when the
        // Generator runs, and give the stub statically typed Buffers;
        // add_output(y) shows the untyped default.
        if (direction == Direction::Quantize) {
            std::vector<Func> encoded = split.offline.outputs();
            assert(encoded.size() == 2);
            add_input<Buffer<float, 1>>(x);
            add_output<Buffer<int8_t, 1>>(encoded[0]);
            add_output<Buffer<float, 1>>(encoded[1]);
        } else {
            assert(split.online_inputs.size() == 2);
            add_input<Buffer<int8_t, 1>>(split.online_inputs[0]);
            add_input<Buffer<float, 1>>(split.online_inputs[1]);
            add_output(y);
        }
    }

    void generate() {
        // Nothing to do: configure() already built and wired up the pipeline.
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ComputeOffline, compute_offline)
