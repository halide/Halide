// A matrix multiply of 8-bit integers on 16x8x32 tiles is the tensor cores'
// mma.sync instruction, whose operands are registers in a layout the
// architecture specifies. There is no instruction that moves such a tile, so
// each lane loads the elements it holds of an operand from where the layout
// says they are, stores its elements of the accumulator likewise, and an
// elementwise function of accumulators reads a matrix in memory at the
// elements the lane holds.
//
// Compiling for CUDA embeds the PTX in the host assembly, so no device is
// needed to check the instructions. On a device, every result is checked
// against data that differs in every row, column and value of K and has
// operands of both signs, so an element in the wrong register would show.

#include "Halide.h"
#include "expect_user_error.h"
#include "halide_test_dirs.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdio.h>

using namespace Halide;

namespace {

const int tm = 16, tn = 8;

// The multiply of tiles of tk values of K.
std::string mma_sync(int tk, const std::string &operands) {
    return "mma.sync.aligned.m16n8k" + std::to_string(tk) + ".row.col.s32." + operands + "." + operands + ".s32";
}

int count(const std::string &s, const std::string &pattern) {
    int n = 0;
    for (size_t i = s.find(pattern); i != std::string::npos; i = s.find(pattern, i + 1)) {
        n++;
    }
    return n;
}

// The instructions that move or multiply wmma's fragments.
int count_wmma(const std::string &s) {
    return count(s, "wmma.load.") + count(s, "wmma.store.") + count(s, "wmma.mma.");
}

Target cuda_target(Target::Feature capability = Target::CUDACapability80) {
    return get_host_target().with_feature(Target::CUDA).with_feature(capability);
}

std::string compile(Func out, const std::vector<Argument> &args, const Target &t) {
    std::string path = Internal::get_test_tmp_dir() + "mma_sync_matmul.s";
    out.compile_to_assembly(path, args, "out", t);
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool can_run(Target *t) {
    *t = get_jit_target_from_environment();
    if (!t->has_feature(Target::CUDA)) {
        return false;
    }
    t->set_feature(Target::CUDACapability80);
    return true;
}

template<typename T>
T operand_value(int x, int y, int salt) {
    return (T)((x * (7 + salt) + y * (13 - salt) + x * y) % 251 - (std::is_signed_v<T> ? 125 : 0));
}

// A 32x16 product over two steps of K in 16x8 tiles of tk values of K. Each
// input is stored either with K
// dense (A's rows, B's columns) or transposed, and the operands are either
// staged in fragments per step of K or loaded at each multiply.
template<typename T>
bool matmul(int tk, bool a_transposed, bool b_transposed, bool staged, Type product_type = Int(32)) {
    const int M = 2 * tm, N = 2 * tn, K = 2 * tk;

    ImageParam A(type_of<T>(), 2, "A"), B(type_of<T>(), 2, "B");
    Var x("x"), y("y"), kk("kk"), yy("yy"), xx("xx"), xi("xi"), yi("yi"), tx("tx"), ty("ty");
    RDom k(0, K, "k");
    RVar ko("ko"), ki("ki");
    Func prod("prod"), out("out"), Am("Am"), Bm("Bm");

    Am(kk, yy) = a_transposed ? A(yy, kk) : A(kk, yy);
    Bm(xx, kk) = b_transposed ? B(kk, xx) : B(xx, kk);
    prod(x, y) = 0;
    prod(x, y) += cast<int32_t>(cast(product_type, Am(k, y)) * cast(product_type, Bm(x, k)));
    out(x, y) = prod(x, y);

    out.bound(x, 0, N)
        .bound(y, 0, M)
        .tile(x, y, xi, yi, N, M)
        .tile(xi, yi, tx, ty, tn, tm)
        .gpu_blocks(x, y)
        .reorder(tx, ty, xi, yi, x, y)
        .unroll(xi)
        .unroll(yi)
        .vectorize(tx)
        .vectorize(ty);
    prod.compute_at(out, x)
        .store_in(MemoryType::Tile)
        .tile(x, y, tx, ty, tn, tm)
        .vectorize(tx)
        .vectorize(ty)
        .unroll(x)
        .unroll(y);
    prod.update()
        .split(k, ko, ki, tk)
        .tile(x, y, tx, ty, tn, tm)
        .reorder(ki, tx, ty, x, y, ko)
        .unroll(x)
        .unroll(y)
        .unroll(ko)
        .atomic()
        .vectorize(ki)
        .vectorize(tx)
        .vectorize(ty);
    if (staged) {
        Var ao("ao"), ai("ai"), bo("bo"), bi("bi");
        Am.compute_at(prod, ko)
            .store_in(MemoryType::Tile)
            .split(kk, ao, ai, tk)
            .split(yy, bo, bi, tm)
            .reorder(ai, bi, ao, bo)
            .unroll(ao)
            .unroll(bo)
            .vectorize(ai)
            .vectorize(bi);
        Bm.compute_at(prod, ko)
            .store_in(MemoryType::Tile)
            .split(xx, ao, ai, tn)
            .split(kk, bo, bi, tk)
            .reorder(ai, bi, ao, bo)
            .unroll(ao)
            .unroll(bo)
            .vectorize(ai)
            .vectorize(bi);
    }

    const std::string assembly = compile(out, {A, B}, cuda_target());
    const std::string s = std::is_signed_v<T> ? "s8" : "u8";
    const int mma = count(assembly, mma_sync(tk, s));
    const int wmma = count_wmma(assembly);
    // Two steps of K of four tiles.
    if (mma != 8 || wmma != 0) {
        printf("Expected 8 %s and no wmma instructions, but got %d and %d\n",
               mma_sync(tk, s).c_str(), mma, wmma);
        return false;
    }

    Target t;
    if (!can_run(&t)) {
        return true;
    }
    Buffer<T> a(a_transposed ? M : K, a_transposed ? K : M), b(b_transposed ? K : N, b_transposed ? N : K);
    a.fill([](int x, int y) { return operand_value<T>(x, y, 0); });
    b.fill([](int x, int y) { return operand_value<T>(x, y, 3); });
    A.set(a);
    B.set(b);
    Buffer<int32_t> result = out.realize({N, M}, t);
    result.copy_to_host();
    for (int yy = 0; yy < M; yy++) {
        for (int xx = 0; xx < N; xx++) {
            int expected = 0;
            for (int kk = 0; kk < K; kk++) {
                expected += (int)(a_transposed ? a(yy, kk) : a(kk, yy)) * (int)(b_transposed ? b(kk, xx) : b(xx, kk));
            }
            if (result(xx, yy) != expected) {
                printf("%s%s%s%s: out(%d, %d) = %d instead of %d\n", mma_sync(tk, s).c_str(),
                       a_transposed ? ", A transposed" : "", b_transposed ? ", B transposed" : "",
                       staged ? ", staged" : "", xx, yy, result(xx, yy), expected);
                return false;
            }
        }
    }
    return true;
}

// Each block of two tiles of K of an int8 matrix multiply is accumulated in an int32
// tile, which is then scaled by a float per row and a float per column into a
// float32 tile. The scales are vectors, so a lane loads the two of the rows
// and the two of the columns that it holds elements of. With `matrix`, the
// tile is also offset by a float32 matrix, of which a lane loads four
// elements.
bool scaled_gemm(int tk, bool matrix) {
    const int M = 2 * tm, N = 2 * tn, block = 2 * tk, K = 2 * block;

    Buffer<int8_t> A(K, M, "A"), B(N, K, "B");
    Buffer<float> sa(M, K / block, "sa"), sb(N, K / block, "sb"), off(N, M, "off");
    A.fill([](int x, int y) { return operand_value<int8_t>(x, y, 0); });
    B.fill([](int x, int y) { return operand_value<int8_t>(x, y, 3); });
    sa.fill([](int x, int y) { return 0.5f + 0.01f * x + 0.02f * y; });
    sb.fill([](int x, int y) { return 1.5f - 0.01f * x + 0.03f * y; });
    off.fill([](int x, int y) { return 100.0f * x - 1000.0f * y; });

    Var x("x"), y("y"), b("b"), xi("xi"), yi("yi"), tx("tx"), ty("ty");
    RDom k(0, block, "k"), rb(0, K / block, "rb");
    RVar ko("ko"), ki("ki");
    Func codes("codes"), acc("acc"), out("out");
    codes(x, y, b) += cast<int32_t>(cast<int16_t>(A(b * block + k, y)) * cast<int16_t>(B(x, b * block + k)));
    acc(x, y) += cast<float>(codes(x, y, rb)) * sa(y, rb) * sb(x, rb) + (matrix ? off(x, y) : 0.0f);
    out(x, y) = acc(x, y);

    out.bound(x, 0, N)
        .bound(y, 0, M)
        .tile(x, y, xi, yi, N, M)
        .tile(xi, yi, tx, ty, tn, tm)
        .gpu_blocks(x, y)
        .reorder(tx, ty, xi, yi, x, y)
        .unroll(xi)
        .unroll(yi)
        .vectorize(tx)
        .vectorize(ty);
    acc.compute_at(out, x)
        .store_in(MemoryType::Tile)
        .tile(x, y, tx, ty, tn, tm)
        .vectorize(tx)
        .vectorize(ty)
        .unroll(x)
        .unroll(y);
    acc.update()
        .tile(x, y, tx, ty, tn, tm)
        .reorder(tx, ty, x, y, rb)
        .unroll(x)
        .unroll(y)
        .atomic()
        .vectorize(tx)
        .vectorize(ty);
    codes.compute_at(acc, rb)
        .store_in(MemoryType::Tile)
        .tile(x, y, tx, ty, tn, tm)
        .vectorize(tx)
        .vectorize(ty)
        .unroll(x)
        .unroll(y)
        .unroll(b);
    codes.update()
        .split(k, ko, ki, tk)
        .tile(x, y, tx, ty, tn, tm)
        .reorder(ki, tx, ty, x, y, ko, b)
        .unroll(x)
        .unroll(y)
        .unroll(ko)
        .unroll(b)
        .atomic()
        .vectorize(ki)
        .vectorize(tx)
        .vectorize(ty);

    const std::string assembly = compile(out, {}, cuda_target());
    const int mma = count(assembly, mma_sync(tk, "s8"));
    const int wmma = count_wmma(assembly);
    // The loop over the blocks holds four tiles of two multiplies each.
    if (mma != 8 || wmma != 0) {
        printf("Expected 8 %s and no wmma instructions, but got %d and %d\n",
               mma_sync(tk, "s8").c_str(), mma, wmma);
        return false;
    }

    Target t;
    if (!can_run(&t)) {
        return true;
    }
    Buffer<float> result = out.realize({N, M}, t);
    result.copy_to_host();
    for (int yy = 0; yy < M; yy++) {
        for (int xx = 0; xx < N; xx++) {
            double expected = 0;
            for (int bb = 0; bb < K / block; bb++) {
                int dot = 0;
                for (int kk = 0; kk < block; kk++) {
                    dot += (int)A(bb * block + kk, yy) * (int)B(xx, bb * block + kk);
                }
                expected += (double)dot * sa(yy, bb) * sb(xx, bb) + (matrix ? off(xx, yy) : 0.0f);
            }
            if (std::fabs(result(xx, yy) - expected) > 1e-3 * (1 + std::fabs(expected))) {
                printf("%s scaled%s: out(%d, %d) = %f instead of %f\n", mma_sync(tk, "s8").c_str(),
                       matrix ? " and offset" : "", xx, yy, result(xx, yy), expected);
                return false;
            }
        }
    }
    return true;
}

// One 16x8 tile of the product of A and B over tk values of K.
Func one_tile(const ImageParam &A, const ImageParam &B, Type acc_type, int tk = 32) {
    Var x("x"), y("y"), xi("xi"), yi("yi");
    RDom k(0, tk, "k");
    Func prod("prod"), out("out");

    prod(x, y) = cast(acc_type, 0);
    prod(x, y) += cast(acc_type, A(k, y)) * cast(acc_type, B(x, k));
    out(x, y) = prod(x, y);

    out.bound(x, 0, tn)
        .bound(y, 0, tm)
        .tile(x, y, xi, yi, tn, tm)
        .gpu_blocks(x, y)
        .vectorize(xi)
        .vectorize(yi);
    prod.compute_at(out, x)
        .store_in(MemoryType::Tile)
        .vectorize(x)
        .vectorize(y);
    prod.update()
        .reorder(k, x, y)
        .atomic()
        .vectorize(k)
        .vectorize(x)
        .vectorize(y);
    return out;
}

}  // namespace

int main(int argc, char **argv) {
    for (int tk : {32, 16}) {
        for (bool a_transposed : {false, true}) {
            for (bool b_transposed : {false, true}) {
                for (bool staged : {false, true}) {
                    if (!matmul<int8_t>(tk, a_transposed, b_transposed, staged) ||
                        !matmul<uint8_t>(tk, a_transposed, b_transposed, staged)) {
                        return 1;
                    }
                }
            }
        }
        // A product computed in 16 bits and widened is the same multiply.
        if (!matmul<int8_t>(tk, false, false, true, Int(16))) {
            return 1;
        }
        if (!scaled_gemm(tk, false) || !scaled_gemm(tk, true)) {
            return 1;
        }
    }

#if HALIDE_WITH_EXCEPTIONS
    int failures = 0;
    // Only the integer multiplies of mma.sync's shapes are lowered to.
    failures += !expect_user_error("half_16x8x32", "16x8x32 or 16x8x16 on 8-bit integers", []() {
        ImageParam A(Float(16), 2, "A"), B(Float(16), 2, "B");
        compile(one_tile(A, B, Float(32)), {A, B}, cuda_target());
    });
    failures += !expect_user_error("half_16x8x16", "16x8x32 or 16x8x16 on 8-bit integers", []() {
        ImageParam A(Float(16), 2, "A"), B(Float(16), 2, "B");
        compile(one_tile(A, B, Float(32), 16), {A, B}, cuda_target());
    });
    // They arrived with compute capability 8.0.
    for (int tk : {32, 16}) {
        failures += !expect_user_error("capability", "cuda_capability_80", [=]() {
            ImageParam A(Int(8), 2, "A"), B(Int(8), 2, "B");
            compile(one_tile(A, B, Int(32), tk), {A, B}, cuda_target(Target::CUDACapability75));
        });
    }
    if (failures) {
        return 1;
    }
#endif

    if (!get_jit_target_from_environment().has_feature(Target::CUDA)) {
        printf("[SKIP] The instructions are as expected, but running them needs CUDA.\n");
        return 0;
    }
    printf("Success!\n");
    return 0;
}
