#include "Halide.h"
#include "halide_test_dirs.h"
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace Halide;

// Matrix multiplies on Apple GPUs' simdgroup_matrix units, which Metal exposes
// as 8x8 matrices striped across the 32 lanes of a SIMD-group. This is the
// same Tile memory lowering as NVIDIA tensor cores use (see wmma_matmul.cpp),
// with the one tile shape Metal has: 8x8x8.

namespace {

constexpr int tile = 8;

struct Params {
    // The type of the two matrices being multiplied. The accumulator is
    // float32 unless half_accumulator asks otherwise.
    Type operand = Float(16);
    // The size of the matrices.
    int M = 64, N = 64, K = 64;
    // How many tiles of accumulator each SIMD-group holds, in each dimension.
    int tiles_m = 1, tiles_n = 1;
    // How many SIMD-groups per threadgroup.
    int warps = 1;
    // Whether each input matrix is stored with its rows or its columns dense.
    bool a_transposed = false, b_transposed = false;
    // Whether to accumulate in half precision instead of single precision.
    // Only available with half precision operands.
    bool half_accumulator = false;
    // Whether to stage the operand tiles through threadgroup memory inside the
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
             << ", " << p.tiles_m << "x" << p.tiles_n << " tiles per SIMD-group, "
             << p.warps << " SIMD-groups, a" << (p.a_transposed ? "T" : "")
             << " b" << (p.b_transposed ? "T" : "")
             << " of " << p.operand
             << (p.half_accumulator ? ", f16 accumulator" : "")
             << (p.stage_in_shared ? ", staged through threadgroup memory" : "")
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
            } else if (t == Float(32)) {
                buf.as<float>()(x, y) = (float)v;
            } else {
                assert(false && "unhandled operand type");
            }
        }
    }
}

double read(const Buffer<> &buf, int x, int y) {
    Type t = buf.type();
    if (t == Float(16)) {
        return (double)Buffer<const float16_t>(buf)(x, y);
    } else if (t == Float(32)) {
        return Buffer<const float>(buf)(x, y);
    }
    assert(false && "unhandled buffer type");
    return 0;
}

Type accumulator_for(const Params &p) {
    return p.half_accumulator ? Float(16) : Float(32);
}

// The matrix multiply of wmma_matmul.cpp, at Metal's one tile shape.
Func make_matmul(const Params &p, const Buffer<> &A, const Buffer<> &B) {
    Var x("x"), y("y"), kk("kk"), xx("xx"), yy("yy");
    RDom k(0, p.K, "k");
    Func prod("prod"), out("out"), Af("Af"), Bf("Bf"), init("init");

    // Halide indexes matrices with the dense dimension first, so A(k, y) is a
    // row-major M x K matrix, and A(y, k) is a column-major one. These are
    // inlined unless we're staging through threadgroup memory.
    Af(kk, yy) = p.a_transposed ? A(yy, kk) : A(kk, yy);
    Bf(xx, kk) = p.b_transposed ? B(kk, xx) : B(xx, kk);
    Type acc_type = accumulator_for(p);
    init(x, y) = cast(acc_type, (x * 3 + y) % 7);
    prod(x, y) = p.init_from_memory ? init(x, y) : cast(acc_type, 0);
    prod(x, y) += cast(acc_type, Af(k, y)) * cast(acc_type, Bf(x, k));
    out(x, y) = prod(x, y);

    Var xi("xi"), yi("yi"), xt("xt"), mmxi("mmxi"), mmyi("mmyi");
    Var rxi("rxi"), ryi("ryi");
    RVar rro("rro"), rri("rri");

    // Each threadgroup computes a tile of the output. Within a threadgroup,
    // each SIMD-group computes a strip of that tile, and each SIMD-group's
    // strip is broken into 8x8 tiles, which are the innermost two (vectorized)
    // dimensions.
    out.bound(x, 0, p.N)
        .bound(y, 0, p.M)
        .split(x, x, xi, tile * p.tiles_n * p.warps)
        .split(xi, xt, xi, tile * p.tiles_n)
        .split(y, y, yi, tile * p.tiles_m)
        .tile(xi, yi, mmxi, mmyi, tile, tile)
        .gpu_blocks(x, y)
        .reorder(mmxi, mmyi, xi, yi, xt, x, y)
        .unroll(xi)
        .unroll(yi)
        .vectorize(mmxi)
        .vectorize(mmyi);
    if (p.warps > 1) {
        out.gpu_threads(xt);
    }

    prod.compute_at(out, xt)
        .store_in(MemoryType::Tile)
        .tile(x, y, rxi, ryi, tile, tile)
        .vectorize(rxi)
        .vectorize(ryi)
        .unroll(x)
        .unroll(y);

    prod.update()
        .tile(x, y, rxi, ryi, tile, tile)
        .split(k, rro, rri, tile)
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
    return out;
}

bool test(const Params &p) {
    Buffer<> A(p.operand, p.a_transposed ? p.M : p.K, p.a_transposed ? p.K : p.M);
    Buffer<> B(p.operand, p.b_transposed ? p.K : p.N, p.b_transposed ? p.N : p.K);
    fill(A);
    fill(B);

    Func out = make_matmul(p, A, B);

    // A transposed output is a view of a buffer with the dimensions swapped, so
    // its columns are dense in memory instead of its rows.
    Type acc_type = accumulator_for(p);
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

// The accumulators can also live outside the loop over SIMD-groups, which is
// what lets the reduction loop sit above it, so that the operand panels can be
// staged into threadgroup memory once per threadgroup and reused by every
// SIMD-group.
bool test_block_level_accumulator() {
    const int M = 128, N = 128, K = 128;
    const int tiles_x = 4, tiles_y = 4, warps = 2, bk = 32;
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

    out.bound(x, 0, N)
        .bound(y, 0, M)
        .tile(x, y, xi, yi, block_x, block_y)
        .split(xi, xt, xi, tile * tiles_x)
        .tile(xi, yi, mmxi, mmyi, tile, tile)
        .gpu_blocks(x, y)
        .gpu_threads(xt)
        .reorder(mmxi, mmyi, xi, yi, xt, x, y)
        .unroll(xi)
        .unroll(yi)
        .vectorize(mmxi)
        .vectorize(mmyi);

    prod.compute_at(out, x)
        .store_in(MemoryType::Tile)
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
        .split(kk, kko, kki, 4)
        .fuse(kko, yy, t)
        .split(t, t, ti, 32)
        .split(t, to, tw, warps)
        .gpu_lanes(ti)
        .gpu_threads(tw)
        .vectorize(kki);
    Bs.compute_at(prod, ko)
        .store_in(MemoryType::GPUShared)
        .split(xx, xxo, xxi, 4)
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
            if (result(i, j) != ref) {
                std::cerr << "Mismatch at " << i << ", " << j << ": "
                          << result(i, j) << " != " << ref << "\n"
                          << "For a block-level accumulator staged through threadgroup memory\n";
                return false;
            }
        }
    }
    return true;
}

// Stage the operand tiles into simdgroup matrices, so that each one loaded
// feeds several multiplies.
bool test_staged_operands(Type operand) {
    const int M = 64, N = 64, K = 64;
    const int tiles_x = 2, tiles_y = 2, bk = 16;

    Buffer<> A(operand, K, M), B(operand, N, K);
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
        .tile(x, y, xi, yi, tile * tiles_x, tile * tiles_y)
        .tile(xi, yi, mmxi, mmyi, tile, tile)
        .gpu_blocks(x, y)
        .reorder(mmxi, mmyi, xi, yi, x, y)
        .unroll(xi)
        .unroll(yi)
        .vectorize(mmxi)
        .vectorize(mmyi);

    prod.compute_at(out, x)
        .store_in(MemoryType::Tile)
        .tile(x, y, rxi, ryi, tile, tile)
        .vectorize(rxi)
        .vectorize(ryi)
        .unroll(x)
        .unroll(y);

    prod.update()
        .split(k, ko, ki, bk)
        .tile(x, y, rxi, ryi, tile, tile)
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
        .store_in(MemoryType::Tile)
        .split(kk, kko, kki, tile)
        .split(yy, yyo, yyi, tile)
        .reorder(kki, yyi, kko, yyo)
        .unroll(kko)
        .unroll(yyo)
        .vectorize(kki)
        .vectorize(yyi);

    // All the b fragments at once, live across the loops over both.
    Bm.compute_at(prod, ki)
        .store_in(MemoryType::Tile)
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
            double ref = 0;
            for (int l = 0; l < K; l++) {
                ref += read(A, l, j) * read(B, i, l);
            }
            if (result(i, j) != ref) {
                std::cerr << "Mismatch at " << i << ", " << j << ": "
                          << result(i, j) << " != " << ref << "\n"
                          << "For " << operand << " operands staged in simdgroup matrices\n";
                return false;
            }
        }
    }
    return true;
}

// Cross-compile for Metal and check that the kernel source, which is embedded
// in the object as a string, uses the simdgroup_matrix operations. This doesn't
// need a GPU.
bool test_codegen() {
    Params p;
    p.tiles_m = p.tiles_n = 2;
    p.b_transposed = true;
    p.stage_in_shared = true;
    Buffer<> A(p.operand, p.K, p.M), B(p.operand, p.K, p.N);
    Func out = make_matmul(p, A, B);
    std::string file = Internal::get_test_tmp_dir() + "metal_simdgroup_matmul.ll";
    Target t("arm-64-macos-metal-no_runtime-no_asserts-no_bounds_query");
    out.compile_to_llvm_assembly(file, {A, B}, t);
    std::ifstream f(file);
    std::stringstream contents;
    contents << f.rdbuf();
    const std::string src = contents.str();
    for (const char *needle : {"simdgroup_multiply_accumulate(", "simdgroup_load(",
                               "simdgroup_store(", "make_filled_simdgroup_matrix<float, 8, 8>(0)",
                               "simdgroup_matrix<half, 8, 8>", "simdgroup_matrix<float, 8, 8>"}) {
        if (src.find(needle) == std::string::npos) {
            std::cerr << "Did not find " << needle << " in the Metal kernel source in " << file << "\n";
            return false;
        }
    }
    // The 2x2 accumulator tiles per SIMD-group and 8-deep reduction steps
    // make four multiplies per step.
    size_t count = 0;
    for (size_t pos = src.find("simdgroup_multiply_accumulate("); pos != std::string::npos;
         pos = src.find("simdgroup_multiply_accumulate(", pos + 1)) {
        count++;
    }
    if (count != 4) {
        std::cerr << "Expected 4 simdgroup_multiply_accumulate calls, found " << count << "\n";
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    if (!test_codegen()) {
        printf("Failed!\n");
        return 1;
    }

    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::Metal)) {
        printf("[SKIP] simdgroup_matrix matrix multiplies require Metal.\n");
        return 0;
    }

    auto make_params = [](const auto &set) {
        Params p;
        set(p);
        return p;
    };

    std::vector<Params> params;
    for (Type t : {Float(16), Float(32)}) {
        // The simplest possible case.
        params.push_back(make_params([&](Params &p) { p.operand = t; }));

        // Each combination of input layouts.
        params.push_back(make_params([&](Params &p) { p.operand = t; p.a_transposed = true; }));
        params.push_back(make_params([&](Params &p) { p.operand = t; p.b_transposed = true; }));
        params.push_back(make_params([&](Params &p) { p.operand = t; p.a_transposed = true; p.b_transposed = true; }));

        // Several accumulator fragments live at once.
        params.push_back(make_params([&](Params &p) { p.operand = t; p.tiles_m = 2; p.tiles_n = 2; }));
        params.push_back(make_params([&](Params &p) { p.operand = t; p.tiles_m = 4; p.tiles_n = 2; }));

        // More than one SIMD-group per threadgroup.
        params.push_back(make_params([&](Params &p) { p.operand = t; p.warps = 2; }));
        params.push_back(make_params([&](Params &p) { p.operand = t; p.tiles_m = 2; p.tiles_n = 2; p.warps = 2; }));

        // Operand tiles staged through threadgroup memory inside the reduction loop.
        params.push_back(make_params([&](Params &p) { p.operand = t; p.stage_in_shared = true; }));
        params.push_back(make_params([&](Params &p) { p.operand = t; p.tiles_m = 2; p.tiles_n = 2; p.stage_in_shared = true; p.b_transposed = true; }));

        // Accumulators that start from a matrix already in memory.
        params.push_back(make_params([&](Params &p) { p.operand = t; p.init_from_memory = true; }));
        params.push_back(make_params([&](Params &p) { p.operand = t; p.tiles_m = 2; p.tiles_n = 2; p.init_from_memory = true; }));
        params.push_back(make_params([&](Params &p) { p.operand = t; p.stage_in_shared = true; p.init_from_memory = true; }));

        // A column-major output.
        params.push_back(make_params([&](Params &p) { p.operand = t; p.out_transposed = true; }));
        params.push_back(make_params([&](Params &p) { p.operand = t; p.tiles_m = 2; p.tiles_n = 2; p.out_transposed = true; }));

        // Matrices that aren't square.
        params.push_back(make_params([&](Params &p) { p.operand = t; p.M = 32; p.N = 128; p.K = 256; }));
    }

    // Accumulating in half precision.
    params.push_back(make_params([&](Params &p) { p.half_accumulator = true; }));
    params.push_back(make_params([&](Params &p) { p.tiles_m = 2; p.tiles_n = 2; p.half_accumulator = true; }));
    params.push_back(make_params([&](Params &p) { p.half_accumulator = true; p.stage_in_shared = true; }));
    params.push_back(make_params([&](Params &p) { p.half_accumulator = true; p.init_from_memory = true; }));
    params.push_back(make_params([&](Params &p) { p.half_accumulator = true; p.out_transposed = true; }));

    for (const Params &p : params) {
        if (!test(p)) {
            printf("Failed!\n");
            return 1;
        }
    }

    if (!test_block_level_accumulator() ||
        !test_staged_operands(Float(16)) ||
        !test_staged_operands(Float(32))) {
        printf("Failed!\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
