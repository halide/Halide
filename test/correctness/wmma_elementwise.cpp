// The lanes of a warp can compute on the elements of a tensor core
// accumulator that they hold, so long as every operand is laid out the same
// way: another accumulator, or a matrix in memory, which is loaded into
// registers as an accumulator is. Here each 32 values of K of an int8 matrix
// multiply are accumulated in an int32 tile, which is then scaled by a float
// per row and a float per column into a float32 tile. The scales are vectors,
// so the matrices they are loaded as have a stride of zero. The int32 tile
// must never go through memory.
//
// Nothing documents that an int32 and a float32 accumulator keep an element in
// the same register of the same lane, so the scaled result is checked on the
// device for every tile shape: the scales differ per row and per column, and
// would land on the wrong elements if the layouts differed.

#include "Halide.h"
#include "expect_user_error.h"
#include "halide_test_dirs.h"
#include <cmath>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdio.h>

using namespace Halide;

namespace {

int count(const std::string &s, const std::string &pattern) {
    int n = 0;
    for (size_t i = s.find(pattern); i != std::string::npos; i = s.find(pattern, i + 1)) {
        n++;
    }
    return n;
}

Target cuda_target(Target::Feature capability = Target::CUDACapability75) {
    return get_host_target().with_feature(Target::CUDA).with_feature(capability);
}

// Compiling for CUDA embeds the PTX in the host assembly, so no device is
// needed to check the instructions.
std::string compile(Func out, const std::vector<Argument> &args, const Target &t) {
    std::string path = Internal::get_test_tmp_dir() + "wmma_elementwise.s";
    out.compile_to_assembly(path, args, "out", t);
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string shape_name(int tm, int tn) {
    return "m" + std::to_string(tm) + "n" + std::to_string(tn) + "k16";
}

// The scaled gemm described above on tiles of tm rows and tn columns.
bool scaled_gemm(int tm, int tn) {
    const int M = 2 * tm, N = 2 * tn, K = 64, block = 32, tk = 16;

    Buffer<int8_t> A(K, M, "A"), B(N, K, "B");
    Buffer<float> sa(M, K / block, "sa"), sb(N, K / block, "sb");
    A.fill([](int x, int y) { return (int8_t)((x * 7 + y * 13) % 31 - 15); });
    B.fill([](int x, int y) { return (int8_t)((x * 5 + y * 11) % 29 - 14); });
    sa.fill([](int x, int y) { return 0.5f + 0.01f * x + 0.02f * y; });
    sb.fill([](int x, int y) { return 1.5f - 0.01f * x + 0.03f * y; });

    Var x("x"), y("y"), b("b"), xi("xi"), yi("yi"), tx("tx"), ty("ty");
    RDom k(0, block, "k"), rb(0, K / block, "rb");
    RVar ko("ko"), ki("ki");
    Func codes("codes"), acc("acc"), out("out");
    // The product of two int8s, computed in 16 bits and then widened.
    codes(x, y, b) += cast<int32_t>(cast<int16_t>(A(b * block + k, y)) * cast<int16_t>(B(x, b * block + k)));
    acc(x, y) += cast<float>(codes(x, y, rb)) * sa(y, rb) * sb(x, rb);
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

    {
        const std::string assembly = compile(out, {}, cuda_target());
        const std::string shape = shape_name(tm, tn);
        const int mma = count(assembly, "wmma.mma.sync.aligned.row.row." + shape + ".s32.s8.s8.s32");
        const int row_scales = count(assembly, "wmma.load.c.sync.aligned.col." + shape + ".f32");
        const int col_scales = count(assembly, "wmma.load.c.sync.aligned.row." + shape + ".f32");
        const int stores = count(assembly, "wmma.store.d.sync.aligned.row." + shape + ".f32");
        const int int_stores = count(assembly, "wmma.store.d.sync.aligned.row." + shape + ".s32");
        // The loop over the blocks holds four tiles of two multiplies each.
        // A block's two row scales and two column scales are each shared by
        // two tiles.
        if (mma != 8 || row_scales != 2 || col_scales != 2 || stores != 4 || int_stores != 0) {
            printf("%s: expected 8 int8 multiplies, 2 loads of row scales, 2 of column scales, "
                   "4 float32 stores and no int32 stores, but got %d, %d, %d, %d and %d\n",
                   shape.c_str(), mma, row_scales, col_scales, stores, int_stores);
            return false;
        }
    }

    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA)) {
        return true;
    }
    t.set_feature(Target::CUDACapability75);
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
                expected += (double)dot * sa(yy, bb) * sb(xx, bb);
            }
            if (std::fabs(result(xx, yy) - expected) > 1e-3 * (1 + std::fabs(expected))) {
                printf("%dx%d tiles: out(%d, %d) = %f instead of %f\n", tm, tn, xx, yy, result(xx, yy), expected);
                return false;
            }
        }
    }
    return true;
}

const int tile = 16;

// One 16x16 tile of the product of A and B, accumulated in a tile of
// `acc_type`, and a second tile that `value` computes from the first.
Func one_tile(const Func &A, const Func &B, Type acc_type,
              const std::function<Expr(Func, Var, Var)> &value) {
    Var x("x"), y("y"), xi("xi"), yi("yi");
    RDom k(0, tile, "k");
    Func prod("prod"), acc("acc"), out("out");

    prod(x, y) = cast(acc_type, 0);
    prod(x, y) += cast(acc_type, A(k, y)) * cast(acc_type, B(x, k));
    acc(x, y) = value(prod, x, y);
    out(x, y) = acc(x, y);

    out.bound(x, 0, tile)
        .bound(y, 0, tile)
        .tile(x, y, xi, yi, tile, tile)
        .gpu_blocks(x, y)
        .vectorize(xi)
        .vectorize(yi);
    for (Func f : {prod, acc}) {
        f.compute_at(out, x)
            .store_in(MemoryType::Tile)
            .vectorize(x)
            .vectorize(y);
    }
    prod.update()
        .reorder(k, x, y)
        .atomic()
        .vectorize(k)
        .vectorize(x)
        .vectorize(y);
    return out;
}

Func wrap(const ImageParam &p) {
    Var u("u"), v("v");
    Func f(p.name() + "_f");
    f(u, v) = p(u, v);
    return f;
}

// An accumulator that is another one converted, or just copied, has nothing
// to do with a matrix in memory: it is the elementwise function of one
// operand.
template<typename T>
bool conversion() {
    Buffer<int8_t> a(tile, tile), b(tile, tile);
    a.fill([](int x, int y) { return (int8_t)((x * 37 + y * 101) % 256 - 128); });
    b.fill([](int x, int y) { return (int8_t)(127 - (x * 59 + y * 13) % 256); });
    ImageParam A(Int(8), 2, "A"), B(Int(8), 2, "B");
    Func out = one_tile(wrap(A), wrap(B), Int(32), [](Func prod, Var x, Var y) {
        return cast<T>(prod(x, y));
    });

    const std::string assembly = compile(out, {A, B}, cuda_target());
    const std::string suffix = type_of<T>().is_float() ? ".f32" : ".s32";
    const int mma = count(assembly, "wmma.mma.sync.aligned.row.row.m16n16k16.s32.s8.s8.s32");
    const int stores = count(assembly, "wmma.store.d.sync.aligned.row.m16n16k16");
    const int typed_stores = count(assembly, "wmma.store.d.sync.aligned.row.m16n16k16" + suffix);
    const int loads = count(assembly, "wmma.load.c");
    if (mma != 1 || stores != 1 || typed_stores != 1 || loads != 0) {
        printf("Expected 1 int8 multiply, 1 store of a%s tile and no loads of an accumulator, "
               "but got %d multiplies, %d stores, %d of them%s, and %d loads\n",
               suffix.c_str(), mma, stores, typed_stores, suffix.c_str(), loads);
        return false;
    }

    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA)) {
        return true;
    }
    t.set_feature(Target::CUDACapability75);
    A.set(a);
    B.set(b);
    Buffer<T> result = out.realize({tile, tile}, t);
    result.copy_to_host();
    for (int y = 0; y < tile; y++) {
        for (int x = 0; x < tile; x++) {
            int expected = 0;
            for (int k = 0; k < tile; k++) {
                expected += (int)a(k, y) * (int)b(x, k);
            }
            if (result(x, y) != (T)expected) {
                printf("Converted to%s: result(%d, %d) = %f instead of %d\n",
                       suffix.c_str(), x, y, (double)result(x, y), expected);
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    for (auto [tm, tn] : {std::pair{16, 16}, std::pair{32, 8}, std::pair{8, 32}}) {
        if (!scaled_gemm(tm, tn)) {
            return 1;
        }
    }
    if (!conversion<float>() || !conversion<int32_t>()) {
        return 1;
    }

#if HALIDE_WITH_EXCEPTIONS
    int failures = 0;
    // One element of an accumulator is in some lane's register, which the
    // other lanes can't read.
    failures += !expect_user_error("scalar_read", "a single element of the accumulator", []() {
        ImageParam A(Int(8), 2, "A"), B(Int(8), 2, "B");
        compile(one_tile(wrap(A), wrap(B), Int(32),
                         [](Func prod, Var x, Var y) {
                             return cast<float>(prod(x, y)) * cast<float>(prod(0, 0));
                         }),
                {A, B}, cuda_target());
    });
    // There is no tensor core load of a uint32 matrix.
    failures += !expect_user_error("uint32_matrix", "not float32 or int32", []() {
        ImageParam A(Int(8), 2, "A"), B(Int(8), 2, "B"), S(UInt(32), 2, "S");
        compile(one_tile(wrap(A), wrap(B), Int(32),
                         [&](Func prod, Var x, Var y) {
                             return cast<float>(prod(x, y)) * cast<float>(S(x, y));
                         }),
                {A, B, S}, cuda_target());
    });
    // The integer fragments arrived after the half precision ones, so a
    // float32 accumulator of a half precision multiply that reads an int32
    // matrix needs a newer device than the multiply does.
    failures += !expect_user_error("int32_matrix_capability", "cuda_capability_75", []() {
        ImageParam A(Float(16), 2, "A"), B(Float(16), 2, "B"), S(Int(32), 2, "S");
        compile(one_tile(wrap(A), wrap(B), Float(32),
                         [&](Func prod, Var x, Var y) {
                             return prod(x, y) * cast<float>(S(x, y));
                         }),
                {A, B, S}, cuda_target(Target::CUDACapability70));
    });
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
