#include "Halide.h"
#include "halide_approximation_codec.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Halide;

namespace {

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                       \
        }                                                                   \
    } while (0)

// Symmetric per-block quantization of (within, block, rest...) to int8 codes
// and a scale per block.
struct BlockQuantizer {
    int block;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Var i("i"), b("b");
        RDom r(0, block);
        Func amax("cq_amax"), scale("cq_scale"), codes("cq_codes");
        amax(b, _) = maximum(abs(in[0](r, b, _)));
        scale(b, _) = amax(b, _) / 127.0f;
        codes(i, b, _) = cast<int8_t>(round(in[0](i, b, _) / select(scale(b, _) == 0.0f, 1.0f, scale(b, _))));
        return {codes, scale};
    }

    std::vector<Func> decode(const std::vector<Func> &encoded) const {
        Var i("i"), b("b");
        Func out("cq_decoded");
        out(i, b, _) = cast<float>(encoded[0](i, b, _)) * encoded[1](b, _);
        return {out};
    }

    ApproximationSignature signature(const ApproximationPorts &inputs) const {
        std::optional<int> dims, scale_dims;
        if (inputs.size() == 1 && inputs[0].dimensions) {
            dims = inputs[0].dimensions;
            scale_dims = *dims - 1;
        }
        return {{{"block", Float(32), dims}},
                {{"codes", Int(8), dims}, {"scale", Float(32), scale_dims}}};
    }
};

constexpr int kBlock = 8;

Approximation make_scheme() {
    Pointwise f16{"f16",
                  [](Expr x) { return cast<float16_t>(x); },
                  [](Expr x) { return cast<float>(x); }};
    return Compose(BlockReshape{kBlock}, BlockQuantizer{kBlock},
                   Parallel{{"codes", Identity{}}, {"scale", f16}});
}

// Records the ports a Generator would declare.
struct FakeGenerator {
    std::vector<std::string> inputs, outputs;
    void add_input(const ImageParam &p) {
        inputs.push_back(p.name());
    }
    void add_output(const std::string &name, const Func &) {
        outputs.push_back(name);
    }
};

float value(int k, int n) {
    return std::sin(k * 0.37f + n) * (1.0f + (k / kBlock));
}

int test_ports() {
    ApproximationCodec::Codec codec = ApproximationCodec::make_codec(make_scheme(), Float(32), 1);
    CHECK(codec.values.name() == "values");
    CHECK(codec.decoded.name() == "decoded");
    CHECK(codec.encoded.size() == 2 && codec.encoded_inputs.size() == 2);
    CHECK(codec.encoded[0].name() == "codes" && codec.encoded[1].name() == "scale");
    for (size_t i = 0; i < 2; i++) {
        CHECK(codec.encoded_inputs[i].name() == codec.encoded[i].name());
        CHECK(codec.encoded_inputs[i].type() == codec.encoded[i].type());
        CHECK(codec.encoded_inputs[i].dimensions() == codec.encoded[i].dimensions());
    }
    CHECK(codec.encoded[1].type() == Float(16));

    FakeGenerator encoder, decoder;
    codec.adopt_encoder(encoder);
    codec.adopt_decoder(decoder);
    CHECK((encoder.inputs == std::vector<std::string>{"values"}));
    CHECK((encoder.outputs == std::vector<std::string>{"codes", "scale"}));
    CHECK((decoder.inputs == std::vector<std::string>{"codes", "scale"}));
    CHECK((decoder.outputs == std::vector<std::string>{"decoded"}));

    // Func names are made unique, so a second codec's Funcs are renamed (as
    // when a Generator's configure() runs once per target), but its ports
    // keep their names.
    ApproximationCodec::Codec again = ApproximationCodec::make_codec(make_scheme(), Float(32), 1);
    CHECK(again.decoded.name() != "decoded" && again.encoded[0].name() != "codes");
    FakeGenerator encoder2, decoder2;
    again.adopt_encoder(encoder2);
    again.adopt_decoder(decoder2);
    CHECK(encoder2.inputs == encoder.inputs && encoder2.outputs == encoder.outputs);
    CHECK(decoder2.inputs == decoder.inputs && decoder2.outputs == decoder.outputs);

    ApproximationCodec::Options options;
    options.values_name = "x";
    options.decoded_name = "y";
    options.encoded_names = {"q", "d"};
    ApproximationCodec::Codec renamed = ApproximationCodec::make_codec(make_scheme(), Float(32), 1, options);
    CHECK(renamed.values.name() == "x" && renamed.decoded.name() == "y");
    CHECK(renamed.encoded_names == (std::vector<std::string>{"q", "d"}) && renamed.decoded_name == "y");
    CHECK(renamed.encoded_inputs[0].name() == "q" && renamed.encoded_inputs[1].name() == "d");
    return 0;
}

// Encode and decode a matrix through the two halves, and compare with the
// unsplit round trip.
int test_round_trip() {
    const int K = 4 * kBlock, N = 3;
    ApproximationCodec::Codec codec = ApproximationCodec::make_codec(make_scheme(), Float(32), 2);

    Buffer<float> values(K, N);
    values.for_each_element([&](int k, int n) { values(k, n) = value(k, n); });
    // An all-zero block exercises the zero-scale path.
    for (int k = 0; k < kBlock; k++) {
        values(k, 1) = 0.0f;
    }
    codec.values.set(values);

    Buffer<int8_t> codes(kBlock, K / kBlock, N);
    Buffer<float16_t> scale(K / kBlock, N);
    Pipeline(codec.encoded).realize({codes, scale});

    codec.encoded_inputs[0].set(codes);
    codec.encoded_inputs[1].set(scale);
    Buffer<float> decoded = codec.decoded.realize({K, N});

    // The same scheme, unsplit.
    Var k("k"), n("n");
    Func w("w"), out("out");
    w(k, n) = Func(values)(k, n);
    out(k, n) = w(k, n);
    ApproximationResult r = w.approximate_by(make_scheme(), {out});
    for (Func f : r.intermediates) {
        if (f.has_update_definition() || r.is_stage_port(f)) {
            f.compute_root();
        }
    }
    Buffer<float> expected = out.realize({K, N});

    for (int j = 0; j < N; j++) {
        for (int i = 0; i < K; i++) {
            if (decoded(i, j) != expected(i, j) || std::abs(decoded(i, j) - values(i, j)) > 0.05f) {
                printf("decoded(%d, %d) = %f, expected %f (value %f)\n", i, j, decoded(i, j),
                       expected(i, j), values(i, j));
                return 1;
            }
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (test_ports() || test_round_trip()) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
