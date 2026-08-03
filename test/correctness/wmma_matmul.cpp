#include "Halide.h"
#include <cstdio>

using namespace Halide;

namespace {

struct Params {
    // The type of the two matrices being multiplied. The accumulator type
    // follows from it: the integer instructions accumulate into int32, and the
    // float ones into float32 unless half_accumulator asks otherwise.
    Type operand = Float(16);
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
    // Only available with half precision operands.
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
             << " of " << p.operand
             << (p.half_accumulator ? ", f16 accumulator" : "")
             << (p.stage_in_shared ? ", staged through shared" : "")
             << (p.init_from_memory ? ", accumulator initialized from memory" : "")
             << (p.out_transposed ? ", transposed output" : "");
}

// Small non-negative integers, which every type here represents exactly, and
// which keep the dot products small enough to be exact in the accumulator too.
// That lets the results be checked for equality rather than to a tolerance.
void fill(Buffer<> buf) {
    Type t = buf.type();
    for (int y = 0; y < buf.height(); y++) {
        for (int x = 0; x < buf.width(); x++) {
            int v = rand() & 3;
            if (t == Float(16)) {
                buf.as<float16_t>()(x, y) = float16_t((float)v);
            } else if (t == BFloat(16)) {
                buf.as<bfloat16_t>()(x, y) = bfloat16_t((float)v);
            } else if (t == Int(8)) {
                buf.as<int8_t>()(x, y) = (int8_t)v;
            } else if (t == UInt(8)) {
                buf.as<uint8_t>()(x, y) = (uint8_t)v;
            } else {
                assert(false && "unhandled operand type");
            }
        }
    }
}

// Read an element of a buffer of any of the types this test uses.
double read(const Buffer<> &buf, int x, int y) {
    Type t = buf.type();
    if (t == Float(16)) {
        return (double)Buffer<const float16_t>(buf)(x, y);
    } else if (t == BFloat(16)) {
        return (double)Buffer<const bfloat16_t>(buf)(x, y);
    } else if (t == Float(32)) {
        return Buffer<const float>(buf)(x, y);
    } else if (t == Int(8)) {
        return Buffer<const int8_t>(buf)(x, y);
    } else if (t == UInt(8)) {
        return Buffer<const uint8_t>(buf)(x, y);
    } else if (t == Int(32)) {
        return Buffer<const int32_t>(buf)(x, y);
    }
    assert(false && "unhandled buffer type");
    return 0;
}

// The accumulator the hardware pairs with a given operand type.
Type accumulator_for(const Params &p) {
    if (p.operand.is_int() || p.operand.is_uint()) {
        return Int(32);
    }
    return p.half_accumulator ? Float(16) : Float(32);
}

bool test(const Params &p) {
    // Halide indexes matrices with the dense dimension first, so A(k, y) is a
    // row-major M x K matrix, and A(y, k) is a column-major one.
    Buffer<> A(p.operand, p.a_transposed ? p.M : p.K, p.a_transposed ? p.K : p.M);
    Buffer<> B(p.operand, p.b_transposed ? p.K : p.N, p.b_transposed ? p.N : p.K);
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
    Type acc_type = accumulator_for(p);
    init(x, y) = cast(acc_type, (x * 3 + y) % 7);
    prod(x, y) = p.init_from_memory ? init(x, y) : cast(acc_type, 0);
    prod(x, y) += cast(acc_type, a) * cast(acc_type, b);
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
        .store_in(MemoryType::WMMAFragment)
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
    Buffer<> result_storage(acc_type, p.out_transposed ? p.M : p.N,
                            p.out_transposed ? p.N : p.M);
    Buffer<> result =
        p.out_transposed ? result_storage.transposed(0, 1) : result_storage;
    out.realize(result);
    result.copy_to_host();

    for (int j = 0; j < p.M; j++) {
        for (int i = 0; i < p.N; i++) {
            double ref = p.init_from_memory ? (double)((i * 3 + j) % 7) : 0.0;
            for (int l = 0; l < p.K; l++) {
                ref += read(A, p.a_transposed ? j : l, p.a_transposed ? l : j) *
                       read(B, p.b_transposed ? l : i, p.b_transposed ? i : l);
            }
            // The operands are small integers and the dot products stay small
            // enough to be exact in every accumulator type here, so the answer
            // should be exact however the GPU orders the accumulation.
            if (read(result, i, j) != ref) {
                std::cerr << "Mismatch at " << i << ", " << j << ": "
                          << read(result, i, j) << " != " << ref << "\n"
                          << "For matmul of " << p << "\n";
                return false;
            }
        }
    }
    return true;
}

// The accumulators can also live outside the loop over warps, which is what
// lets the reduction loop sit above it, so that the operand panels can be
// staged into shared memory once per block and reused by every warp.
bool test_block_level_accumulator() {
    const int M = 128, N = 128, K = 128;
    const int tile = 16, tiles_x = 2, tiles_y = 2, warps = 2, bk = 32;
    const int block_x = tile * tiles_x * warps, block_y = tile * tiles_y;

    Buffer<float16_t> A(K, M), B(N, K);
    fill(A);
    fill(B);

    Var x("x"), y("y"), kk("kk"), yy("yy"), xx("xx");
    RDom k(0, K, "k");
    Func prod("prod"), out("out"), As("As"), Bs("Bs");

    As(kk, yy) = A(kk, yy);
    Bs(xx, kk) = B(xx, kk);
    prod(x, y) = 0.f;
    prod(x, y) += cast<float>(As(k, y)) * cast<float>(Bs(x, k));
    out(x, y) = prod(x, y);

    Var xi("xi"), xt("xt"), yi("yi"), mmxi("mmxi"), mmyi("mmyi");
    Var rxi("rxi"), ryi("ryi"), xw("xw"), t("t"), ti("ti"), tw("tw"), to("to");
    Var kko("kko"), kki("kki"), xxo("xxo"), xxi("xxi");
    RVar ko("ko"), ki("ki"), rri("rri");

    out.bound(x, 0, N).bound(y, 0, M).split(x, x, xi, block_x).split(xi, xt, xi, tile * tiles_x).split(xi, xi, mmxi, tile).split(y, y, yi, block_y).split(yi, yi, mmyi, tile).gpu_blocks(x, y).gpu_threads(xt).reorder(mmxi, mmyi, xi, yi, xt, x, y).unroll(xi).unroll(yi).vectorize(mmxi).vectorize(mmyi);

    prod.compute_at(out, x)
        .store_in(MemoryType::WMMAFragment)
        .split(x, xw, xi, tile * tiles_x)
        .split(xi, xi, rxi, tile)
        .split(y, y, ryi, tile)
        .reorder(rxi, ryi, xi, y, xw)
        .gpu_threads(xw)
        .vectorize(rxi)
        .vectorize(ryi)
        .unroll(xi)
        .unroll(y);

    prod.update()
        .split(k, ko, ki, bk)
        .split(x, xw, xi, tile * tiles_x)
        .split(xi, xi, rxi, tile)
        .split(y, y, ryi, tile)
        .split(ki, ki, rri, tile)
        .reorder(rri, rxi, ryi, xi, y, ki, xw, ko)
        .gpu_threads(xw)
        .unroll(xi)
        .unroll(y)
        .unroll(ki)
        .atomic()
        .vectorize(rri)
        .vectorize(rxi)
        .vectorize(ryi);

    As.compute_at(prod, ko)
        .store_in(MemoryType::GPUShared)
        .split(kk, kko, kki, 8)
        .fuse(kko, yy, t)
        .split(t, t, ti, 32)
        .split(t, to, tw, warps)
        .gpu_lanes(ti)
        .gpu_threads(tw)
        .vectorize(kki);
    Bs.compute_at(prod, ko)
        .store_in(MemoryType::GPUShared)
        .split(xx, xxo, xxi, 8)
        .fuse(xxo, kk, t)
        .split(t, t, ti, 32)
        .split(t, to, tw, warps)
        .gpu_lanes(ti)
        .gpu_threads(tw)
        .vectorize(xxi);

    Buffer<float> result(N, M);
    out.realize(result);
    result.copy_to_host();

    for (int j = 0; j < M; j++) {
        for (int i = 0; i < N; i++) {
            float ref = 0.f;
            for (int l = 0; l < K; l++) {
                ref += (float)A(l, j) * (float)B(i, l);
            }
            if (std::abs(result(i, j) - ref) > 1e-2f * std::max(1.f, std::abs(ref))) {
                std::cerr << "Mismatch at " << i << ", " << j << ": "
                          << result(i, j) << " != " << ref << "\n"
                          << "For a block-level accumulator staged through shared memory\n";
                return false;
            }
        }
    }
    return true;
}

// Stage the operand tiles into fragment registers, so that each fragment
// loaded feeds several multiplies. Where each staging happens says how much
// reuse we get out of it.
bool test_staged_operands() {
    const int M = 128, N = 128, K = 64;
    const int tile = 16, tiles_x = 2, tiles_y = 2, bk = 32;

    Buffer<float16_t> A(K, M), B(N, K);
    fill(A);
    fill(B);

    Var x("x"), y("y"), kk("kk"), yy("yy"), xx("xx");
    RDom k(0, K, "k");
    Func prod("prod"), out("out"), Am("Am"), Bm("Bm");

    Am(kk, yy) = A(kk, yy);
    Bm(xx, kk) = B(xx, kk);
    prod(x, y) = 0.f;
    prod(x, y) += cast<float>(Am(k, y)) * cast<float>(Bm(x, k));
    out(x, y) = prod(x, y);

    Var xi("xi"), yi("yi"), mmxi("mmxi"), mmyi("mmyi"), rxi("rxi"), ryi("ryi");
    Var kko("kko"), kki("kki"), yyo("yyo"), yyi("yyi"), xxo("xxo"), xxi("xxi");
    RVar ko("ko"), ki("ki"), rri("rri");

    out.bound(x, 0, N)
        .bound(y, 0, M)
        .split(x, x, xi, tile * tiles_x)
        .split(xi, xi, mmxi, tile)
        .split(y, y, yi, tile * tiles_y)
        .split(yi, yi, mmyi, tile)
        .gpu_blocks(x, y)
        .reorder(mmxi, mmyi, xi, yi, x, y)
        .unroll(xi)
        .unroll(yi)
        .vectorize(mmxi)
        .vectorize(mmyi);

    prod.compute_at(out, x)
        .store_in(MemoryType::WMMAFragment)
        .split(x, x, rxi, tile)
        .split(y, y, ryi, tile)
        .vectorize(rxi)
        .vectorize(ryi)
        .unroll(x)
        .unroll(y);

    // Loop nest of the update, outermost first: ko, ki, y, x.
    prod.update()
        .split(k, ko, ki, bk)
        .split(x, x, rxi, tile)
        .split(y, y, ryi, tile)
        .split(ki, ki, rri, tile)
        .reorder(rri, rxi, ryi, x, y, ki, ko)
        .unroll(x)
        .unroll(y)
        .unroll(ki)
        .atomic()
        .vectorize(rri)
        .vectorize(rxi)
        .vectorize(ryi);

    // One a fragment per row of tiles, live across the loop over columns.
    Am.compute_at(prod, y)
        .store_in(MemoryType::WMMAFragment)
        .split(kk, kko, kki, tile)
        .split(yy, yyo, yyi, tile)
        .reorder(kki, yyi, kko, yyo)
        .unroll(kko)
        .unroll(yyo)
        .vectorize(kki)
        .vectorize(yyi);

    // All the b fragments at once, live across the loops over both.
    Bm.compute_at(prod, ki)
        .store_in(MemoryType::WMMAFragment)
        .split(xx, xxo, xxi, tile)
        .split(kk, kko, kki, tile)
        .reorder(xxi, kki, xxo, kko)
        .unroll(xxo)
        .unroll(kko)
        .vectorize(xxi)
        .vectorize(kki);

    Buffer<float> result(N, M);
    out.realize(result);
    result.copy_to_host();

    for (int j = 0; j < M; j++) {
        for (int i = 0; i < N; i++) {
            float ref = 0.f;
            for (int l = 0; l < K; l++) {
                ref += (float)A(l, j) * (float)B(i, l);
            }
            if (std::abs(result(i, j) - ref) > 1e-2f * std::max(1.f, std::abs(ref))) {
                std::cerr << "Mismatch at " << i << ", " << j << ": "
                          << result(i, j) << " != " << ref << "\n"
                          << "For operands staged in fragment registers\n";
                return false;
            }
        }
    }
    return true;
}

// A batch of matrix multiplies that all share the same left-hand side. Its
// fragments only need loading once for the whole batch, which is what staging
// them outside the loop over the batch does. The loop isn't unrolled, so
// nothing downstream of here could hoist them out of it.
bool test_operand_hoisted_out_of_loop() {
    const int M = 32, N = 32, K = 16, batch = 4;
    const int tile = 16;

    Buffer<float16_t> A(K, M), B(N, K, batch);
    fill(A);
    fill(B);

    Var x("x"), y("y"), n("n"), kk("kk"), yy("yy");
    RDom k(0, K, "k");
    Func prod("prod"), out("out"), Am("Am");

    Am(kk, yy) = A(kk, yy);
    prod(x, y, n) = 0.f;
    prod(x, y, n) += cast<float>(Am(k, y)) * cast<float>(B(x, k, n));
    out(x, y, n) = prod(x, y, n);

    Var xi("xi"), yi("yi"), mmxi("mmxi"), mmyi("mmyi"), rxi("rxi"), ryi("ryi");
    Var kko("kko"), kki("kki"), yyo("yyo"), yyi("yyi");
    RVar rro("rro"), rri("rri");

    out.bound(x, 0, N)
        .bound(y, 0, M)
        .bound(n, 0, batch)
        .split(x, x, xi, N)
        .split(xi, xi, mmxi, tile)
        .split(y, y, yi, M)
        .split(yi, yi, mmyi, tile)
        .gpu_blocks(x, y)
        .reorder(mmxi, mmyi, xi, yi, n, x, y)
        .unroll(xi)
        .unroll(yi)
        .vectorize(mmxi)
        .vectorize(mmyi);

    prod.compute_at(out, n)
        .store_in(MemoryType::WMMAFragment)
        .split(x, x, rxi, tile)
        .split(y, y, ryi, tile)
        .vectorize(rxi)
        .vectorize(ryi)
        .unroll(x)
        .unroll(y);

    prod.update()
        .split(x, x, rxi, tile)
        .split(y, y, ryi, tile)
        .split(k, rro, rri, tile)
        .reorder(rri, rxi, ryi, x, y, rro)
        .unroll(x)
        .unroll(y)
        .atomic()
        .vectorize(rri)
        .vectorize(rxi)
        .vectorize(ryi);

    Am.compute_at(out, x)
        .store_in(MemoryType::WMMAFragment)
        .split(kk, kko, kki, tile)
        .split(yy, yyo, yyi, tile)
        .reorder(kki, yyi, kko, yyo)
        .unroll(kko)
        .unroll(yyo)
        .vectorize(kki)
        .vectorize(yyi);

    Buffer<float> result(N, M, batch);
    out.realize(result);
    result.copy_to_host();

    for (int b = 0; b < batch; b++) {
        for (int j = 0; j < M; j++) {
            for (int i = 0; i < N; i++) {
                float ref = 0.f;
                for (int l = 0; l < K; l++) {
                    ref += (float)A(l, j) * (float)B(i, l, b);
                }
                if (std::abs(result(i, j, b) - ref) > 1e-2f * std::max(1.f, std::abs(ref))) {
                    std::cerr << "Mismatch at " << i << ", " << j << ", " << b << ": "
                              << result(i, j, b) << " != " << ref << "\n"
                              << "For an operand staged outside a loop over a batch\n";
                    return false;
                }
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

    // The other operand types the hardware multiplies. Brain floats accumulate
    // into single precision, and eight-bit integers into 32-bit ones, which is
    // the interesting case for imaging.
    for (Type t : {BFloat(16), Int(8), UInt(8)}) {
        params.push_back({.operand = t});
        params.push_back({.operand = t, .tile_m = 32, .tile_n = 8});
        params.push_back({.operand = t, .tile_m = 8, .tile_n = 32});
        params.push_back({.operand = t, .tiles_m = 2, .tiles_n = 2});
        params.push_back({.operand = t, .warps = 2});
        params.push_back({.operand = t, .a_transposed = true});
        params.push_back({.operand = t, .b_transposed = true});
        params.push_back({.operand = t, .stage_in_shared = true});
        params.push_back({.operand = t, .init_from_memory = true});
        params.push_back({.operand = t, .out_transposed = true});
    }

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

    if (!test_block_level_accumulator() ||
        !test_staged_operands() ||
        !test_operand_hoisted_out_of_loop()) {
        printf("Failed!\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
