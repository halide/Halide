#include "Halide.h"
#include <cstdio>

using namespace Halide;

namespace {

struct Params {
    // The size of the matrices.
    int M = 64, N = 64, K = 64;
    // The tensor core tile shape to use.
    int tile_m = 16, tile_n = 16, tile_k = 16;
    // How many tiles of accumulator each warp holds, in each dimension.
    int tiles_m = 1, tiles_n = 1;
    // How many warps per block.
    int warps = 1;
    // Whether each input matrix is stored with its rows or its columns dense.
    bool a_transposed = false, b_transposed = false;
    // Whether to accumulate in half precision instead of single precision.
    bool half_accumulator = false;
    // Whether to stage the operand tiles through shared memory inside the
    // reduction loop.
    bool stage_in_shared = false;
    // Whether to start the accumulator from a matrix in memory rather than
    // from zero.
    bool init_from_memory = false;
    // Whether the output is stored with its columns dense rather than its rows.
    bool out_transposed = false;
};

std::ostream &operator<<(std::ostream &s, const Params &p) {
    return s << p.M << "x" << p.N << "x" << p.K
             << " in " << p.tile_m << "x" << p.tile_n << "x" << p.tile_k
             << " tiles, " << p.tiles_m << "x" << p.tiles_n << " tiles per warp, "
             << p.warps << " warps, a" << (p.a_transposed ? "T" : "")
             << " b" << (p.b_transposed ? "T" : "")
             << (p.half_accumulator ? ", f16 accumulator" : "")
             << (p.stage_in_shared ? ", staged through shared" : "")
             << (p.init_from_memory ? ", accumulator initialized from memory" : "")
             << (p.out_transposed ? ", transposed output" : "");
}

void fill(Buffer<float16_t> &buf) {
    buf.fill([]() {
        return float16_t(((float)rand() / RAND_MAX) - 0.5f);
    });
}

bool test(const Params &p) {
    // Halide indexes matrices with the dense dimension first, so A(k, y) is a
    // row-major M x K matrix, and A(y, k) is a column-major one.
    Buffer<float16_t> A(p.a_transposed ? p.M : p.K, p.a_transposed ? p.K : p.M);
    Buffer<float16_t> B(p.b_transposed ? p.K : p.N, p.b_transposed ? p.N : p.K);
    fill(A);
    fill(B);

    Var x("x"), y("y"), kk("kk"), xx("xx"), yy("yy");
    RDom k(0, p.K, "k");
    Func prod("prod"), out("out"), Af("Af"), Bf("Bf"), init("init");

    // These are inlined unless we're staging through shared memory.
    Af(kk, yy) = p.a_transposed ? A(yy, kk) : A(kk, yy);
    Bf(xx, kk) = p.b_transposed ? B(kk, xx) : B(xx, kk);
    Expr a = Af(k, y);
    Expr b = Bf(x, k);
    // With a half-precision accumulator the tile is copied out to memory as
    // float16, so the output has to be float16 too.
    // The accumulator either starts at zero, or at a matrix that already exists
    // in memory, which the hardware can load straight into the fragments.
    Type acc_type = p.half_accumulator ? Float(16) : Float(32);
    init(x, y) = cast(acc_type, (x * 3 + y) % 7) * cast(acc_type, 0.25f);
    if (p.half_accumulator) {
        prod(x, y) = p.init_from_memory ? init(x, y) : cast<float16_t>(0.f);
        prod(x, y) += a * b;
    } else {
        prod(x, y) = p.init_from_memory ? init(x, y) : Expr(0.f);
        prod(x, y) += cast<float>(a) * cast<float>(b);
    }
    out(x, y) = prod(x, y);

    Var xi("xi"), yi("yi"), xt("xt"), mmxi("mmxi"), mmyi("mmyi");
    Var rxi("rxi"), ryi("ryi");
    RVar rro("rro"), rri("rri");

    // Each block computes a tile of the output. Within a block, each warp
    // computes a strip of that tile, and each warp's strip is broken into
    // tensor core tiles, which are the innermost two (vectorized) dimensions.
    out.bound(x, 0, p.N)
        .bound(y, 0, p.M)
        .split(x, x, xi, p.tile_n * p.tiles_n * p.warps)
        .split(xi, xt, xi, p.tile_n * p.tiles_n)
        .split(xi, xi, mmxi, p.tile_n)
        .split(y, y, yi, p.tile_m * p.tiles_m)
        .split(yi, yi, mmyi, p.tile_m)
        .gpu_blocks(x, y)
        .reorder(mmxi, mmyi, xi, yi, xt, x, y)
        .unroll(xi)
        .unroll(yi)
        .vectorize(mmxi)
        .vectorize(mmyi);
    if (p.warps > 1) {
        // With one warp there's no need for a loop over warps, and leaving it
        // serial keeps anything computed inside it out of the thread loops.
        out.gpu_threads(xt);
    }

    prod.compute_at(out, xt)
        .store_in(MemoryType::WMMAAccumulator)
        .split(x, x, rxi, p.tile_n)
        .split(y, y, ryi, p.tile_m)
        .vectorize(rxi)
        .vectorize(ryi)
        .unroll(x)
        .unroll(y);

    prod.update()
        .split(x, x, rxi, p.tile_n)
        .split(y, y, ryi, p.tile_m)
        .split(k, rro, rri, p.tile_k)
        .reorder(rri, rxi, ryi, x, y, rro)
        .unroll(x)
        .unroll(y)
        .atomic()
        .vectorize(rri)
        .vectorize(rxi)
        .vectorize(ryi);

    if (p.init_from_memory) {
        Var ixi("ixi"), iyi("iyi");
        init.compute_root().gpu_tile(x, y, ixi, iyi, 16, 16);
    }

    if (p.stage_in_shared) {
        Var t("t"), ti("ti"), to("to");
        Af.compute_at(prod, rro)
            .store_in(MemoryType::GPUShared)
            .fuse(kk, yy, t)
            .split(t, to, ti, 32)
            .gpu_lanes(ti);
        Bf.compute_at(prod, rro)
            .store_in(MemoryType::GPUShared)
            .fuse(xx, kk, t)
            .split(t, to, ti, 32)
            .gpu_lanes(ti);
    }

    if (p.out_transposed) {
        out.output_buffer().dim(0).set_stride(p.M).dim(1).set_stride(1);
    }

    // A transposed output is a view of a buffer with the dimensions swapped, so
    // its columns are dense in memory instead of its rows.
    Buffer<float> result_storage(p.out_transposed ? p.M : p.N,
                                 p.out_transposed ? p.N : p.M);
    Buffer<float> result =
        p.out_transposed ? result_storage.transposed(0, 1) : result_storage;
    Buffer<float16_t> result_half(p.N, p.M);
    auto get = [&](int i, int j) {
        return p.half_accumulator ? (float)result_half(i, j) : result(i, j);
    };
    if (p.half_accumulator) {
        out.realize(result_half);
        result_half.copy_to_host();
    } else {
        out.realize(result);
        result.copy_to_host();
    }

    for (int j = 0; j < p.M; j++) {
        for (int i = 0; i < p.N; i++) {
            float ref = p.init_from_memory ? (float)(((i * 3 + j) % 7) * 0.25f) : 0.f;
            for (int l = 0; l < p.K; l++) {
                ref += (float)(p.a_transposed ? A(j, l) : A(l, j)) *
                       (float)(p.b_transposed ? B(l, i) : B(i, l));
            }
            // The accumulation happens in a different order on the GPU, and
            // the inputs are half-precision, so allow some slack.
            float tolerance = p.half_accumulator ? 5e-2f : 1e-2f;
            if (std::abs(get(i, j) - ref) > tolerance * std::max(1.f, std::abs(ref))) {
                std::cerr << "Mismatch at " << i << ", " << j << ": "
                          << get(i, j) << " != " << ref << "\n"
                          << "For matmul of " << p << "\n";
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::CUDA)) {
        printf("[SKIP] WMMA matrix multiplies require CUDA.\n");
        return 0;
    }
    if (target.get_cuda_capability_lower_bound() < 70) {
        printf("[SKIP] WMMA matrix multiplies require CUDA compute capability 7.0 or above.\n");
        return 0;
    }

    std::vector<Params> params;

    // The simplest possible case.
    params.push_back({});

    // Each of the other supported tile shapes.
    params.push_back({.tile_m = 32, .tile_n = 8});
    params.push_back({.tile_m = 8, .tile_n = 32});

    // Each combination of input layouts.
    params.push_back({.a_transposed = true});
    params.push_back({.b_transposed = true});
    params.push_back({.a_transposed = true, .b_transposed = true});

    // Several accumulator fragments live at once, which is what you want in
    // practice so that each operand load feeds more than one multiply.
    params.push_back({.tiles_m = 2, .tiles_n = 2});

    // More than one warp per block.
    params.push_back({.warps = 2});
    params.push_back({.tiles_m = 2, .tiles_n = 2, .warps = 2});

    // Accumulating in half precision, which uses half as many registers per
    // accumulator fragment.
    params.push_back({.half_accumulator = true});
    params.push_back({.tiles_m = 2, .tiles_n = 2, .half_accumulator = true});

    // Operand tiles staged through shared memory inside the reduction loop.
    // This only works if the loop over lanes wraps the individual wmma
    // statements rather than the whole accumulator allocation.
    params.push_back({.stage_in_shared = true});
    params.push_back({.tiles_m = 2, .tiles_n = 2, .stage_in_shared = true});
    params.push_back({.half_accumulator = true, .stage_in_shared = true});

    // Accumulators that start from a matrix already in memory rather than from
    // zero, which uses the load-into-the-accumulator-fragment instruction.
    params.push_back({.init_from_memory = true});
    params.push_back({.tiles_m = 2, .tiles_n = 2, .init_from_memory = true});
    params.push_back({.half_accumulator = true, .init_from_memory = true});
    params.push_back({.stage_in_shared = true, .init_from_memory = true});

    // A column-major output, which the accumulator is stored to with the other
    // layout of the store instruction.
    params.push_back({.out_transposed = true});
    params.push_back({.tiles_m = 2, .tiles_n = 2, .out_transposed = true});

    // A reduction that isn't a whole number of tiles per unrolled step, and
    // matrices that aren't square.
    params.push_back({.M = 32, .N = 128, .K = 256});

    for (const Params &p : params) {
        if (!test(p)) {
            printf("Failed!\n");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
