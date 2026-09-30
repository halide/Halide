// Halide tutorial lesson 25: Approximations (GGML's Q4_0 weight format)

// This lesson shows how to describe a lossy, quantized data format with
// Halide's Approximation system, using GGML's Q4_0 block format as the
// running example. By the end we will have:
//   - built Q4_0 out of reusable components,
//   - spliced it into an existing pipeline (a dot product) with
//     Func::approximate_by,
//   - split the pipeline into an offline quantizer and an online consumer
//     with Pipeline::compute_offline,
//   - checked that the bytes we produce are bit-for-bit what GGML produces,
//   - and tested the properties the scheme claims.

// On linux, you can compile and run it like so:
// g++ lesson_25*.cpp -g -I <path/to/include> -I <path/to/tools> -L <path/to/lib> -lHalide -lpthread -ldl -o lesson_25 -std=c++17
// LD_LIBRARY_PATH=<path/to/lib> ./lesson_25

// On macOS:
// g++ lesson_25*.cpp -g -I <path/to/include> -I <path/to/tools> -L <path/to/lib> -lHalide -o lesson_25 -std=c++17
// DYLD_LIBRARY_PATH=<path/to/lib> ./lesson_25

// Halide.h contains the Approximation machinery, but the property-testing
// helpers live in a separate header-only tool, like halide_image_io.h. That
// header is used with a plain #include of Halide.h.
#include "Halide.h"
#include "halide_approximation_testing.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

using namespace Halide;
using namespace Halide::ApproximationTesting;

// ----------------------------------------------------------------------------
// Part 0: The plain C++ reference.
//
// This is a transcription of quantize_row_q4_0_ref and dequantize_row_q4_0
// from GGML's ggml-quants.c. We will use it at the end to check the
// Approximation bit-for-bit.
// ----------------------------------------------------------------------------

constexpr int QK4_0 = 32;

// assert() disappears in release builds, so use our own.
void require(bool ok, const char *what) {
    if (!ok) {
        printf("Check failed: %s\n", what);
        exit(1);
    }
}

struct block_q4_0 {
    uint16_t d;  // the fp16 scale, as raw bits
    uint8_t qs[QK4_0 / 2];
};
static_assert(sizeof(block_q4_0) == 18, "block_q4_0 must be 18 bytes");

void reference_quantize(const float *x, block_q4_0 *y, int nblocks) {
    for (int i = 0; i < nblocks; i++) {
        float amax = 0.0f;
        float max = 0.0f;
        for (int j = 0; j < QK4_0; j++) {
            float v = x[i * QK4_0 + j];
            if (amax < std::fabs(v)) {
                amax = std::fabs(v);
                max = v;
            }
        }
        const float d = max / -8;
        const float id = d != 0.0f ? 1.0f / d : 0.0f;
        y[i].d = float16_t(d).to_bits();
        for (int j = 0; j < QK4_0 / 2; j++) {
            // Separate statements, so the compiler can't fuse them into an fma.
            const float x0 = x[i * QK4_0 + j] * id;
            const float x1 = x[i * QK4_0 + QK4_0 / 2 + j] * id;
            const uint8_t xi0 = std::min<int>(15, (int8_t)(x0 + 8.5f));
            const uint8_t xi1 = std::min<int>(15, (int8_t)(x1 + 8.5f));
            y[i].qs[j] = xi0 | (xi1 << 4);
        }
    }
}

void reference_dequantize(const block_q4_0 *x, float *y, int nblocks) {
    for (int i = 0; i < nblocks; i++) {
        const float d = (float)float16_t::make_from_bits(x[i].d);
        for (int j = 0; j < QK4_0 / 2; j++) {
            y[i * QK4_0 + j] = ((x[i].qs[j] & 0x0F) - 8) * d;
            y[i * QK4_0 + j + QK4_0 / 2] = ((x[i].qs[j] >> 4) - 8) * d;
        }
    }
}

// Deterministic test data. Most blocks are pseudo-random, but the first few
// are tricky on purpose.
std::vector<float> make_weights(int nblocks) {
    std::vector<float> w(static_cast<size_t>(nblocks) * QK4_0);
    uint32_t state = 12345;
    for (float &v : w) {
        state = state * 1664525u + 1013904223u;
        v = ((int32_t)(state >> 8) / (float)(1 << 23) - 1.0f) * 3.0f;
    }
    for (int j = 0; j < QK4_0; j++) {
        w[0 * QK4_0 + j] = 0.0f;                              // all zeros: d == 0
        w[1 * QK4_0 + j] = (j % 2 != 0 ? -1 : 1) * 0.25f * (j % 8);  // ties: +/-1.75 repeat
        w[2 * QK4_0 + j] = -0.5f - 0.1f * j;                  // the extreme is negative
        w[3 * QK4_0 + j] = 100.0f;                            // constant block
        w[4 * QK4_0 + j] = 1e-20f * (j - 7);                  // tiny scale
    }
    return w;
}

// ----------------------------------------------------------------------------
// Part 1: What is an Approximation?
//
// A quantized weight format is an *approximate identity* factored in two:
//     decode(encode(x)) ~= x
// `encode` is run once, offline, to compress the weights; `decode` runs every
// time the weights are used. An Approximation bundles the pair so that a
// scheme can't get out of sync with itself.
//
// The smallest possible Approximation is a Pointwise unit: an elementwise
// Expr -> Expr function for each direction.
// ----------------------------------------------------------------------------

void part1_a_tiny_approximation() {
    // Drop the low bit of an integer: lossy, with error at most 1.
    Approximation drop_lsb =
        Pointwise{"drop_lsb",
                  [](const Expr &x) { return x >> 1; },
                  [](const Expr &x) { return x << 1; }}
            .with_error_bound([](const Expr &) { return Expr(1); });

    Var x("x");
    Func f("f"), consumer("consumer");
    f(x) = x;
    consumer(x) = f(x) + 100;

    // approximate_by rewrites `consumer` so that wherever it used f(x), it now
    // uses decode(encode(f))(x). Note that it edits the algorithm: unlike a
    // schedule directive, this changes the values that the pipeline computes.
    f.approximate_by(drop_lsb, {consumer});

    Buffer<int> out = consumer.realize({8});
    for (int i = 0; i < 8; i++) {
        require(out(i) == 100 + (i & ~1), "drop_lsb round trip");
    }
}

// ----------------------------------------------------------------------------
// Part 2: Q4_0 out of reusable components.
//
// Q4_0 stores each block of 32 floats as an fp16 scale `d` and sixteen bytes
// of 4-bit codes. We can read off the encode direction as a pipeline of small
// steps, each of which is an Approximation of its own. Compose takes them
// in the order encode runs them, and decode runs them backwards:
//
//   BlockReshape{32}        flat floats -> (within, block)
//   Q4_0Quantizer           (within, block) -> int8 codes [-8, 7], fp32 scale
//   offset                  codes [-8, 7] -> nibbles [0, 15]
//   PlanarFieldPack{4, 16}  nibbles -> 16 bytes, with element j in the low
//                           half of byte j and element j+16 in the high half
//   fp16                    fp32 scale -> fp16 scale
//   StructLayout            {qs, d} -> one 18-byte record
//
// Everything but the quantizer is generic. The quantizer is where Q4_0's
// arithmetic lives, and it is ordinary client code: a struct with an encode
// and a decode method (and, optionally, declarations about itself), which
// converts implicitly to an Approximation.
// ----------------------------------------------------------------------------

struct Q4_0Quantizer {
    // Encode makes both the codes and the scale, so it takes and returns
    // vectors of Funcs.
    static std::vector<Func> encode(const std::vector<Func> &in) {
        const Func &blocks = in[0];
        Var j("j"), b("b");

        // The signed element of largest magnitude in each block. The first
        // one wins ties, so the comparison is strict.
        RDom r(0, QK4_0);
        Func extreme("extreme");
        extreme(b) = Tuple(0.0f, 0.0f);
        Expr v = blocks(r, b);
        Expr bigger = abs(v) > extreme(b)[0];
        extreme(b) = Tuple(select(bigger, abs(v), extreme(b)[0]),
                           select(bigger, v, extreme(b)[1]));

        Func scale("scale"), codes("codes");
        scale(b) = extreme(b)[1] / -8.0f;
        Expr inv_scale = select(scale(b) != 0.0f, 1.0f / scale(b), 0.0f);
        // One add of 8.5f, as in GGML: (int8_t)(x * id + 8.5f).
        Expr biased = cast<int8_t>(blocks(j, b) * inv_scale + 8.5f);
        codes(j, b) = min(biased, cast<int8_t>(15)) - 8;
        return {codes, scale};
    }

    static std::vector<Func> decode(const std::vector<Func> &encoded) {
        const Func &codes = encoded[0], &scale = encoded[1];
        Var j("j"), b("b");
        Func values("values");
        values(j, b) = codes(j, b) * scale(b);
        return {values};
    }

    // Optional: the types and dimensions of the ports, and a guarantee about
    // the codes. Later stages' preconditions are checked against it.
    static ApproximationSignature signature() {
        return {{{"blocks", Float(32), 2}},
                {{"codes", Int(8), 2, ApproximationRange(-8, 7)},
                 {"scale", Float(32), 1}}};
    }

    // Optional: |decode(encode(x)) - x| is at most one step (the top code is
    // clamped: a value that scales to +8 becomes 7), plus slack for rounding.
    static Func error_bound(const std::vector<Func> & /*inputs*/, const std::vector<Func> &encoded) {
        Var j("j"), b("b");
        Func bound("bound");
        bound(j, b) = abs(cast<double>(encoded[1](b))) * Expr(1.0001);
        return bound;
    }
};

// Pointwise units are elementwise conversions, written inline. This one
// shifts the signed codes into the unsigned nibbles that PlanarFieldPack packs.
// The declarations are optional: the types are checked, the input range is a
// precondition for being lossless, and the output range is a guarantee.
Pointwise make_offset() {
    return Pointwise{"offset",
                     [](const Expr &x) { return cast<uint8_t>(x + 8); },
                     [](const Expr &x) { return cast<int8_t>(cast<int>(x) - 8); }}
        .with_types(Int(8), UInt(8))
        .with_ranges(ApproximationRange(-8, 7), ApproximationRange(0, 15))
        .with_lossless();
}

// Likewise, a cast: the fp32 scale is stored as fp16.
Pointwise make_fp16() {
    return Pointwise{"fp16",
                     [](const Expr &x) { return cast<float16_t>(x); },
                     [](const Expr &x) { return cast<float>(x); }}
        .with_types(Float(32), Float(16));
}

// A scheme is found again later (to schedule it, or to read back one of its
// intermediate values) through the handles of the stages it was built from, so
// we keep the interesting ones around next to the finished scheme.
struct Q4_0 {
    Approximation quantize = Q4_0Quantizer{};
    Approximation offset = Approximation(make_offset(), "offset");
    Approximation pack = PlanarFieldPack{4, QK4_0 / 2};
    Approximation fp16 = Approximation(make_fp16(), "fp16");

    // block_q4_0, as a Halide struct type. Its size is 18 bytes, and it has
    // the same layout as the C++ struct above.
    static Type block_type() {
        return Type::Struct({{"d", Float(16)}, {"qs", UInt(8), 16}});
    }

    // Stages are listed in encode order; decode runs them backwards. Parallel
    // routes each named port to its own child, and passes the others through.
    // A port keeps its name in both directions, so "codes" and "scale" are the
    // right handles on the decode side too.
    Approximation scheme = Compose{
        BlockReshape{QK4_0},
        quantize,
        Parallel{{"codes", Compose{offset, pack}},
                 {"scale", fp16}},
        StructLayout{block_type(), {"qs", "d"}}};
};

// ----------------------------------------------------------------------------
// Part 3: Using it.
//
// Suppose we already have a pipeline: the dot product of a weight vector with
// an activation vector. It was written against float weights, and we'd like
// to store them as Q4_0.
// ----------------------------------------------------------------------------

int main() {
    part1_a_tiny_approximation();

    const int nblocks = 256;
    const int N = nblocks * QK4_0;

    // The pipeline as originally written:
    ImageParam weights_in(Float(32), 1, "weights_in"), acts_in(Float(32), 1, "acts_in");
    Var k("k");
    Func weights("weights");
    weights(k) = weights_in(k);
    RDom r(0, N, "r");
    Func dot("dot");
    dot() = 0.0f;
    dot() += weights(r) * acts_in(r);

    // Now approximate the weights, and splice the round trip into `dot`. The
    // ApproximationResult describes everything that was created.
    Q4_0 q;
    Approximation q4_0 = q.scheme;

    // Printing an Approximation shows its structure, with the type and range
    // of every port, without running anything:
    //
    // Compose (values x1) -> (record: struct{d: float16, qs: uint8[16]} x1)
    //   BlockReshape (values x1) -> (blocks x2)
    //   Q4_0Quantizer (blocks: float32 x2) -> (codes: int8 x2 in [-8, 7], scale: float32 x1)
    //   Parallel (codes: int8 x2 in [-8, 7], scale: float32 x1) -> (bytes: uint8 x2, scale: float16 x1)
    //     Compose (codes: int8 x2 in [-8, 7]) -> (bytes: uint8 x2)
    //       offset (codes: int8 x2 in [-8, 7]) -> (codes: uint8 x2 in [0, 15])
    //       PlanarFieldPack (codes x2 in [0, 15]) -> (bytes: uint8 x2)
    //     fp16 (scale: float32 x1) -> (scale: float16 x1)
    //   StructLayout (bytes: uint8 x2, scale: float16 x1) -> (record: struct{d: float16, qs: uint8[16]} x1)
    std::cout << q4_0 << "\n";
    ApproximationResult approx = weights.approximate_by(q4_0, {dot});

    // Printing `approx` (try it!) shows the tree of stages that ran, with the
    // Funcs each one produced and their helper Funcs, e.g.:
    //
    // encode:
    //   Q4_0Quantizer -> codes=codes, scale=scale
    //     intermediates: extreme
    //   ...

    // Anything with an update definition (like the per-block search for the
    // largest element, `extreme`) or that is the boundary between two stages is an
    // ordinary Func. Here, we compute those at root. The rest are pure, and
    // get inlined into their consumers.
    for (Func f : approx.intermediates) {
        if (f.has_update_definition() || approx.is_stage_port(f)) {
            f.compute_root();
        }
    }

    // Find one particular Func by the handle of the stage that made it. The
    // per-block scale that Q4_0 divides by is an fp32 value, before it is
    // rounded to fp16 for storage.
    Func fp32_scale = approx.encoded_by(q.quantize, "scale");

    // ------------------------------------------------------------------------
    // Part 4: Offline and online.
    //
    // Quantizing is done once, when a model is converted. The dot product is
    // done every time it's run. compute_offline severs the pipeline at the
    // encoded weights: `split.offline` computes them, and the pipeline we
    // gave it now reads them from an ImageParam instead.
    // ------------------------------------------------------------------------
    ComputeOfflineResult split = Pipeline(dot).compute_offline(approx.encoded);

    std::vector<float> weights_data = make_weights(nblocks);
    std::vector<float> acts_data(N);
    for (int i = 0; i < N; i++) {
        acts_data[i] = std::sin(0.01f * i);
    }
    weights_in.set(Buffer<float>(weights_data.data(), N));
    acts_in.set(Buffer<float>(acts_data.data(), N));

    // Offline: quantize. The result is one struct per block.
    Buffer<> encoded(Q4_0::block_type(), nblocks);
    split.offline.realize(encoded);

    // Online: from here on, nothing looks at the fp32 weights.
    split.online_inputs[0].set(encoded);
    Buffer<float> dot_result = dot.realize();
    Buffer<float> dequantized = approx.replacement.realize({N});

    // ------------------------------------------------------------------------
    // Part 5: Is it really Q4_0?
    //
    // Compare against the C++ reference, byte for byte.
    // ------------------------------------------------------------------------
    std::vector<block_q4_0> ref_blocks(nblocks);
    reference_quantize(weights_data.data(), ref_blocks.data(), nblocks);
    std::vector<float> ref_dequantized(N);
    reference_dequantize(ref_blocks.data(), ref_dequantized.data(), nblocks);

    require(encoded.size_in_bytes() == nblocks * sizeof(block_q4_0), "encoded size");
    if (memcmp(encoded.data(), ref_blocks.data(), nblocks * sizeof(block_q4_0)) != 0) {
        for (int b = 0; b < nblocks; b++) {
            const uint8_t *got = (const uint8_t *)encoded.data() + static_cast<size_t>(b) * 18;
            if (memcmp(got, &ref_blocks[b], 18) != 0) {
                printf("block %d differs:\n  got:      ", b);
                for (int i = 0; i < 18; i++) {
                    printf("%02x ", got[i]);
                }
                printf("\n  expected: ");
                for (int i = 0; i < 18; i++) {
                    printf("%02x ", ((const uint8_t *)&ref_blocks[b])[i]);
                }
                printf("\n");
                break;
            }
        }
        printf("Encoded blocks do not match the reference\n");
        return 1;
    }

    for (int i = 0; i < N; i++) {
        // Compare bit patterns: the match must be exact.
        uint32_t got_bits = 0, want_bits = 0;
        memcpy(&got_bits, &dequantized.data()[i], sizeof(got_bits));
        memcpy(&want_bits, &ref_dequantized[i], sizeof(want_bits));
        if (got_bits != want_bits) {
            printf("Dequantized values do not match the reference\n");
            return 1;
        }
    }

    float ref_dot = 0.0f;
    for (int i = 0; i < N; i++) {
        const float product = ref_dequantized[i] * acts_data[i];
        ref_dot += product;
    }
    printf("dot = %f (reference %f)\n", dot_result(), ref_dot);
    // The dot product isn't bit-exact, since Halide may fuse the multiply
    // and add differently than our C++ compiler does. Only the bytes of the
    // format are contractual.
    if (std::fabs(dot_result() - ref_dot) > 1e-5f * std::fabs(ref_dot)) {
        printf("The dot product does not match the reference\n");
        return 1;
    }

    // The fp32 scale before it was rounded to fp16 is also available, since
    // we kept a handle on the quantizer: it's the `d` of the reference.
    Buffer<float> scales = fp32_scale.realize({nblocks});
    for (int b = 0; b < nblocks; b++) {
        float amax = 0.0f, max = 0.0f;
        for (int j = 0; j < QK4_0; j++) {
            float v = weights_data[b * QK4_0 + j];
            if (amax < std::fabs(v)) {
                amax = std::fabs(v);
                max = v;
            }
        }
        require(scales(b) == max / -8, "fp32 scale");
    }

    // Likewise for the values that the decoder saw: what the quantizer's
    // decode direction produced, as (within, block).
    Func decoded_blocks = approx.decoded_by(q.quantize);
    Buffer<float> blocks = decoded_blocks.realize({QK4_0, nblocks});
    for (int b = 0; b < nblocks; b++) {
        for (int j = 0; j < QK4_0; j++) {
            require(blocks(j, b) == ref_dequantized[b * QK4_0 + j], "decoded_by");
        }
    }

    // ------------------------------------------------------------------------
    // Part 6: Checking what the scheme claims.
    //
    // Every unit declares the type and range of the values on its ports. The
    // ranges are never checked when running, but we can check statically that
    // a stage's guarantees imply the next one's preconditions...
    // ------------------------------------------------------------------------
    require(check_ranges(q4_0).empty(), "static range check");

    Distribution normal = Distribution::normal(0, 1);

    // ...and, on random data, get a report of how much the round trip loses.
    std::cout << verify_round_trip(q4_0, normal, Float(32), {N}, 1);

    // ...and check them dynamically by running the scheme on random inputs.
    // ".at(stage)" restricts a property to the values that arrive at just
    // that stage, as computed by the stages before it: packing nibbles is
    // lossless, provided that the quantizer really does produce [0, 15].
    require(check_property(q4_0, lossless().at(q.pack), normal, Float(32), {N}).passed, "property");
    require(check_property(q4_0, lossless().at(q.offset), normal, Float(32), {N}).passed, "property");

    // The quantizer is lossy, but it declares a bound of one step.
    require(check_property(q4_0, within_declared_bound().at(q.quantize), normal, Float(32), {N}).passed, "property");

    printf("Success!\n");
    return 0;
}
