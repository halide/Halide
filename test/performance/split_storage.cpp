#include "Halide.h"
#include "halide_benchmark.h"

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace Halide;
using namespace Halide::Tools;

// Demonstrates a performance win from split_storage: swizzling a staged matmul
// panel so it feeds a dot-product instruction (x86 vpdpbusd) with a plain
// contiguous load instead of an in-loop shuffle.
//
// We compute C(x, y) = sum_k A(k, y) * B(x, k) as an 8-bit -> 32-bit matmul.
// vpdpbusd reduces runs of 4 adjacent k, so to feed it the four reduction
// elements (k..k+3) for each output lane x must be adjacent in memory, i.e.
// the staged B panel must be laid out [x][ki] with ki (the run of 4) innermost.
//
//   2D panel  : B.in() stored [x][k]  -- the vpdpbusd operand must be built
//               each iteration by interleaving strided lanes (a vpunpck/shuffle
//               in the inner loop).
//   swizzled  : B.in() stored [ki][x][ko] via split_storage(k -> ko, ki, 4) --
//               the operand is exactly the contiguous panel layout, so the
//               shuffle is done once during the pack and the hot loop is
//               shuffle-free.
//
// The [ko][x][ki] block structure cannot be expressed by reorder_storage (which
// only permutes the (x, k) axes); it requires splitting the k storage axis.

namespace {

const int N = 1024;

// swizzle: false = 2D [x][k] panel, true = split_storage [ki][x][ko] panel.
Func build(bool swizzle, ImageParam A, ImageParam B) {
    Var x("x"), y("y");
    RDom k(0, N);

    Func prod("prod");
    prod(x, y) += cast<int32_t>(A(k, y)) * cast<int32_t>(B(x, k));

    const int vec = 16;  // int32 lanes
    RVar ko("ko"), ki("ki");
    prod.compute_root();
    prod.update()
        .split(k, ko, ki, 4)
        .reorder(ki, x, y, ko)
        .vectorize(x, vec)
        .atomic()
        .vectorize(ki, 4);

    Func Bp = B.in(prod);
    std::vector<Var> b = Bp.args();  // b[0] = x, b[1] = k
    Bp.compute_at(prod, ko);
    if (swizzle) {
        Var po("po"), pi("pi");
        Bp.split_storage(b[1], po, pi, 4).reorder_storage(pi, b[0], po);
    }
    Bp.vectorize(b[0], vec);
    return prod;
}

double measure(Func &m, Buffer<int32_t> &out) {
    m.realize(out);  // compile + warm up
    std::vector<double> s;
    for (int i = 0; i < 11; i++) {
        s.push_back(benchmark(1, 3, [&]() { m.realize(out); }));
    }
    std::sort(s.begin(), s.end());
    return s[s.size() / 2];
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless under the WebAssembly interpreter.\n");
        return 0;
    }
    // The win comes from feeding an x86 VNNI dot-product (vpdpbusd), which
    // requires the reduction elements grouped in runs of four.
    if (!(target.arch == Target::X86 && target.has_feature(Target::AVX512_Zen4))) {
        printf("[SKIP] Test targets x86 AVX512 VNNI (vpdpbusd).\n");
        return 0;
    }

    Buffer<uint8_t> matA(N, N);  // A(k, y)
    Buffer<int8_t> matB(N, N);   // B(x, k)
    for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
            matA(i, j) = (i + 2 * j) % 7;
            matB(i, j) = (3 * i + j) % 5 - 2;
        }
    }

    ImageParam A(UInt(8), 2, "A"), B(Int(8), 2, "B");
    A.set(matA);
    B.set(matB);

    Func flat = build(false, A, B);
    Func swizzled = build(true, A, B);
    Buffer<int32_t> out_flat(N, N), out_swizzled(N, N);

    // Warm up to reach steady state, then measure interleaved.
    for (int i = 0; i < 3; i++) {
        flat.realize(out_flat);
        swizzled.realize(out_swizzled);
    }
    double t_flat = measure(flat, out_flat);
    double t_swizzled = measure(swizzled, out_swizzled);

    const double gop = 2.0 * N * N * N / 1e9;
    printf("2D panel [x][k]      : %6.2f ms  %7.2f GOP/s\n", t_flat * 1e3, gop / t_flat);
    printf("swizzled [ki][x][ko] : %6.2f ms  %7.2f GOP/s\n", t_swizzled * 1e3, gop / t_swizzled);
    printf("swizzled speedup: %.2fx\n", t_flat / t_swizzled);

    // Correctness: both layouts must agree and match a naive reference.
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            if (out_flat(x, y) != out_swizzled(x, y)) {
                printf("FAIL: layouts disagree at (%d, %d): %d vs %d\n",
                       x, y, out_flat(x, y), out_swizzled(x, y));
                return 1;
            }
        }
    }
    for (int s = 0; s < 32; s++) {
        int x = (s * 61) % N, y = (s * 97 + 11) % N;
        int64_t ref = 0;
        for (int k = 0; k < N; k++) {
            ref += (int)matA(k, y) * (int)matB(x, k);
        }
        if (ref != out_swizzled(x, y)) {
            printf("FAIL: incorrect result at (%d, %d): %d vs ref %lld\n",
                   x, y, out_swizzled(x, y), (long long)ref);
            return 1;
        }
    }

    // The swizzled panel removes an in-loop shuffle per vpdpbusd, worth ~1.2x
    // here. Gate conservatively for noise.
    if (t_swizzled > t_flat / 1.10) {
        printf("FAIL: swizzled panel was not at least 1.10x faster (%.2fx)\n",
               t_flat / t_swizzled);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
