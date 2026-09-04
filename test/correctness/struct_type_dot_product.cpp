#include "Halide.h"
#include "halide_test_dirs.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <new>
#include <random>
#include <sstream>

using namespace Halide;

namespace {

// The block layouts of ggml's q4_0 (4-bit weights) and q8_0 (8-bit
// activations) quantization formats, expressed as Halide struct types.
//
//   block_q4_0 { fp16 d; uint8 qs[16]; }   // 32 quants, 2 nibbles per byte
//   block_q8_0 { fp16 d; int8  qs[32]; }   // 32 quants
//
// This mirrors ggml_vec_dot_q4_0_q8_0 (ggml-cpu/arch/arm/quants.c): for each
// pair of blocks, the 16 q4_0 bytes hold 32 signed 4-bit weights (low nibbles
// map to quants 0..15, high nibbles to 16..31, each biased by -8), dotted with
// the 32 int8 q8_0 activations, scaled by the product of the two fp16 deltas,
// and summed across all blocks.
//
// The *algorithm* is written as the obvious thing: dequantize each side to
// float on its own -- x_wt for the 4-bit weights, y_wt for the int8
// activations, each scaling by its block's fp16 delta -- and sum the products
// over all quants. result's update is then just
//   result() += x_wt(k, b) * y_wt(k, b)
// a plain float dot product over a single quant index k in [0, 32), with none
// of the packed-integer kernel structure spelled out.
//
// The efficient kernel is then *derived by the schedule*, composing the four
// quantization directives plus a split of the quant reduction:
//
//   * split(r_quant, ko, ki, 16) cuts the 32 quants into the two nibble halves
//     ko and the packed byte ki. Because x_wt indexes qs by k % 16 and picks
//     its nibble by k < 16, unrolling ko folds those to a constant nibble and a
//     dense byte index, so both qs arrays load as whole vectors over ki rather
//     than a strided gather.
//   * rfactor(r_block, u) splits the (block, quant) reduction so each block's
//     quants reduce into their own partial sum, partial(u).
//   * eager_inline(x_wt, y_wt) substitutes the two dequantizers into partial's
//     update, surfacing the per-block deltas xd(u), yd(u) and the integer
//     weight/activation terms as explicit leaves hoist_invariants() can see.
//   * hoist_invariants() lifts xd(u) and yd(u) -- invariant across the quants
//     of a block -- out of the reduction, leaving a scale-free integer dot
//     dot(u) and a single scale multiply in the write-back.
//   * change_type(Int(32)) proves the per-block term can't overflow Int(32) and
//     retypes dot's accumulation accordingly, so the quants reduce with
//     smull/addv instead of a float round-trip.
//
// x_wt and y_wt are left unscheduled: eager_inline() has already folded them
// into the reduction, so nothing else computes them.
Type q4_0_type() {
    return Type::Struct({{"d", Float(16)}, {"qs", UInt(8), 16}});
}
Type q8_0_type() {
    return Type::Struct({{"d", Float(16)}, {"qs", Int(8), 32}});
}
Type q5_0_type() {
    // Deliberately packed: qh is a four-byte scalar at the unaligned offset 2.
    return Type::Struct({{"d", Float(16)}, {"qh", UInt(32)}, {"qs", UInt(8), 16}});
}

struct Pipeline {
    ImageParam x{q4_0_type(), 1, "x"};  // nb q4_0 weight blocks
    ImageParam y{q8_0_type(), 1, "y"};  // nb q8_0 activation blocks
    Func result{"qdot"};

    Pipeline() {
        // Alignment hints: dense, base-aligned blocks.
        x.dim(0).set_min(0);
        y.dim(0).set_min(0);
        x.set_host_alignment(16);
        y.set_host_alignment(16);

        Var b("b"), k("k"), u("u");

        // Dequantize one 4-bit weight to float: quant k is a nibble of the
        // packed byte k % 16 -- its low nibble for k < 16, its high nibble
        // otherwise -- biased by -8 to signed [-8, 7], times the block delta.
        Expr nib = field(x(b), "qs")[k % 16];  // uint8
        Expr w = cast<int32_t>(select(k < 16, nib % 16, nib / 16)) - 8;
        Func x_wt("x_wt");
        x_wt(k, b) = cast<float>(field(x(b), "d")) * cast<float>(w);

        // Dequantize one int8 activation to float: quant k is int8 k, times the
        // block delta.
        Expr a = cast<int32_t>(field(y(b), "qs")[k]);
        Func y_wt("y_wt");
        y_wt(k, b) = cast<float>(field(y(b), "d")) * cast<float>(a);

        // The algorithm: dequantize each side and sum every product.
        RDom r(0, x.dim(0).extent(), 0, 32, "r");  // block, quant
        result() = 0.0f;
        result() += x_wt(r[1], r[0]) * y_wt(r[1], r[0]);

        // Derive the packed-integer kernel from the schedule. Split the 32
        // quants into the two nibble halves ko and the packed byte ki first, so
        // the derived stages carry that structure through.
        RVar ko("ko"), ki("ki");
        result.update().split(r[1], ko, ki, 16);
        Func partial = result.update().rfactor(r[0], u);
        Func dot = partial.update().eager_inline(x_wt, y_wt).hoist_invariants();
        Func dot_i32 = dot.change_type(Int(32));

        // One block's scaled contribution per outer step; its quants reduce
        // horizontally, vectorized across the packed byte and unrolled over the
        // two nibble halves.
        partial.compute_at(result, r[0]);
        dot_i32.compute_at(partial, u)
            .update()
            .reorder(ki, ko, u)
            .atomic()
            .vectorize(ki, 16)
            .unroll(ko);
    }
};

Target arm_target() {
    return Target{"arm-64-linux-no_asserts-no_runtime-no_bounds_query-arm_dot_prod-arm_fp16"};
}

std::string read_file(const std::string &path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int count_occurrences(const std::string &haystack, const std::string &needle) {
    int n = 0;
    for (size_t pos = haystack.find(needle); pos != std::string::npos; pos = haystack.find(needle, pos + 1)) {
        n++;
    }
    return n;
}

// A heap allocation aligned to 16 bytes, since the pipeline declares a
// 16-byte host alignment on its inputs and plain std::vector's allocator
// does not guarantee that (only happens to on some 64-bit ABIs).
struct AlignedBytes {
    explicit AlignedBytes(size_t n)
        : n(n), ptr(static_cast<uint8_t *>(::operator new(n, std::align_val_t(16)))) {
    }
    ~AlignedBytes() {
        ::operator delete(ptr, std::align_val_t(16));
    }
    AlignedBytes(const AlignedBytes &) = delete;
    AlignedBytes &operator=(const AlignedBytes &) = delete;

    uint8_t *data() {
        return ptr;
    }
    const uint8_t *data() const {
        return ptr;
    }
    uint8_t &operator[](size_t i) {
        return ptr[i];
    }
    size_t size() const {
        return n;
    }

private:
    size_t n;
    uint8_t *ptr;
};

// ---- Reference implementation (scalar, for correctness checking) ----
float reference_dot(const AlignedBytes &x_blocks,
                    const AlignedBytes &y_blocks, int nb) {
    // x block = 18 bytes {fp16 d, u8 qs[16]}; y block = 34 bytes {fp16 d, i8 qs[32]}.
    float sumf = 0.0f;
    for (int i = 0; i < nb; i++) {
        const uint8_t *xb = x_blocks.data() + i * 18;
        const uint8_t *yb = y_blocks.data() + i * 34;
        float16_t xd = float16_t::make_from_bits(xb[0] | (xb[1] << 8));
        float16_t yd = float16_t::make_from_bits(yb[0] | (yb[1] << 8));
        int isum = 0;
        for (int j = 0; j < 16; j++) {
            int nib = xb[2 + j];
            int x_lo = (nib % 16) - 8;
            int x_hi = (nib / 16) - 8;
            int y_lo = (int8_t)yb[2 + j];
            int y_hi = (int8_t)yb[2 + j + 16];
            isum += x_lo * y_lo + x_hi * y_hi;
        }
        sumf += (float)xd * (float)yd * (float)isum;
    }
    return sumf;
}

void test_correctness() {
    const int nb = 7;
    std::mt19937 rng(12345);
    AlignedBytes x_data((size_t)nb * 18), y_data((size_t)nb * 34);
    for (size_t i = 0; i < x_data.size(); i++)
        x_data[i] = (uint8_t)(rng() & 0xff);
    for (size_t i = 0; i < y_data.size(); i++)
        y_data[i] = (uint8_t)(rng() & 0xff);
    // Give the two deltas modest, exactly-representable fp16 values so the
    // reference and Halide agree bit-for-bit-ish.
    for (int i = 0; i < nb; i++) {
        uint16_t xd = float16_t(0.5f + 0.25f * i).to_bits();
        uint16_t yd = float16_t(1.0f + 0.5f * i).to_bits();
        memcpy(&x_data[i * 18], &xd, 2);
        memcpy(&y_data[i * 34], &yd, 2);
    }

    halide_dimension_t xs[1] = {{0, nb, 1, 0}};
    halide_dimension_t ys[1] = {{0, nb, 1, 0}};
    Buffer<> xbuf(q4_0_type(), x_data.data(), 1, xs);
    Buffer<> ybuf(q8_0_type(), y_data.data(), 1, ys);

    Pipeline p;
    p.x.set(xbuf);
    p.y.set(ybuf);
    Buffer<float> out = p.result.realize();

    float expected = reference_dot(x_data, y_data, nb);
    if (std::abs(out() - expected) > 1e-2f * std::max(1.0f, std::abs(expected))) {
        printf("q4_0/q8_0 dot product = %f, expected %f\n", out(), expected);
        exit(1);
    }
}

void test_arm_codegen() {
    Pipeline p;
    std::string s_path = Internal::get_test_tmp_dir() + "struct_type_dot_product.s";
    p.result.compile_to_assembly(s_path, {p.x, p.y}, "qdot", arm_target());
    std::string asm_text = read_file(s_path);

    // The struct's packed qs arrays -- 16 q4_0 nibble bytes and 32 q8_0 int8s
    // per block -- must load as whole 128-bit vectors, not element-by-element.
    // A regression to per-field byte loads would show up as a burst of `ldrb`
    // in the block loop; a good lowering emits `ldr`/`ldur q` (or `ld1`).
    int byte_loads = count_occurrences(asm_text, "ldrb");
    int vector_loads = count_occurrences(asm_text, "ldr\tq") +
                       count_occurrences(asm_text, "ldur\tq") +
                       count_occurrences(asm_text, "ld1\t");
    // The fp16 deltas should convert inline (native fcvt), not via a byte load
    // + soft-float path.
    int fp16_converts = count_occurrences(asm_text, "fcvt\ts");

    bool ok = byte_loads == 0 && vector_loads >= 3 && fp16_converts >= 2;
    if (!ok) {
        printf("Unexpected q4_0/q8_0 dot-product ARM codegen: byte_loads=%d (want 0), "
               "vector_loads=%d (want >= 3), fp16_converts=%d (want >= 2).\n",
               byte_loads, vector_loads, fp16_converts);
        printf("---- ARM assembly ----\n%s\n", asm_text.c_str());
        exit(1);
    }
}

void test_packed_scalar_field_codegen() {
    ImageParam blocks(q5_0_type(), 1, "blocks");
    Var b("b");
    Func qh("read_qh");
    qh(b) = cast<uint32_t>(field(blocks(b), "qh"));

    std::string s_path = Internal::get_test_tmp_dir() + "struct_type_packed_scalar.s";
    qh.compile_to_assembly(s_path, {blocks}, "read_qh", arm_target());
    std::string asm_text = read_file(s_path);

    // The packed UInt(32) field starts at byte offset two and is therefore
    // unaligned. It should nevertheless remain one wide load; decomposing it
    // into four byte loads obscures common reuse by subsequent bit extracts.
    int byte_loads = count_occurrences(asm_text, "ldrb");
    int word_loads = count_occurrences(asm_text, "ldr\tw") +
                     count_occurrences(asm_text, "ldur\tw");
    if (byte_loads != 0 || word_loads < 1) {
        printf("Unexpected packed UInt(32) struct-field ARM codegen: byte_loads=%d (want 0), "
               "word_loads=%d (want >= 1).\n",
               byte_loads, word_loads);
        printf("---- ARM assembly ----\n%s\n", asm_text.c_str());
        exit(1);
    }
}

}  // namespace

int main(int argc, char **argv) {
    test_correctness();
    test_arm_codegen();
    test_packed_scalar_field_codegen();
    printf("Success!\n");
    return 0;
}
