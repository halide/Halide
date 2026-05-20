// Exercises the user-facing error paths in ExtractTileOperations.cpp. Each
// scenario below is the most natural-looking pattern that triggers a particular
// error. This doubles as a TODO list of patterns we'd ideally support but
// currently reject.
//
// The test verifies that each scenario produces a Halide::CompileError
// (a user error) rather than crashing or hitting an internal assert.

#include "Halide.h"
#include <stdio.h>

#if HALIDE_WITH_EXCEPTIONS

using namespace Halide;

namespace {

const Target amx_target("x86-64-linux-avx512_sapphirerapids");

// Run `body` and assert it produces a Halide user error.
template<typename F>
bool expect_user_error(const char *name, F body) {
    try {
        body();
    } catch (const CompileError &e) {
        printf("[%s] OK: %s\n", name, e.what());
        return true;
    } catch (...) {
        printf("[%s] FAIL: expected a CompileError but got a different exception\n", name);
        return false;
    }
    printf("[%s] FAIL: expected a user error but none was raised\n", name);
    return false;
}

// Apply a stock AMX matmul schedule to `mm` (with reduction var `r`) and
// the given tile sizes.
void schedule_matmul(Func mm, RVar r, int tile_x, int tile_y, int tile_r) {
    Var x("x"), y("y"), rxi("rxi"), ryi("ryi");
    RVar rri("rri"), rro("rro");
    mm.compute_at(mm.in(), x)
        .store_in(MemoryType::AMXTile)
        .update()
        .tile(x, y, rxi, ryi, tile_x, tile_y, TailStrategy::GuardWithIf)
        .split(r, rro, rri, tile_r)
        .reorder(rri, rxi, ryi, rro, x, y)
        .atomic()
        .vectorize(rri)
        .vectorize(rxi)
        .vectorize(ryi);

    Var ixi("ixi"), iyi("iyi");
    mm.compute_at(mm.in(), x)
        .tile(x, y, ixi, iyi, tile_x, tile_y)
        .vectorize(ixi)
        .vectorize(iyi);

    Var mmxi("mmxi"), mmyi("mmyi");
    mm.in()
        .tile(x, y, mmxi, mmyi, tile_x, tile_y)
        .vectorize(mmxi)
        .vectorize(mmyi);
}

// A tile too large for an AMX register (rows > 16). Triggers the explicit
// row-count check in convert_to_matmul.
void scenario_too_large() {
    Buffer<int8_t> A(64, 64);
    Buffer<int8_t> B(4, 64, 16);
    Var x("x"), y("y");
    RDom r(0, 64, "r");

    Func mm("matmul_large");
    mm(x, y) = cast<int32_t>(0);
    mm(x, y) += cast<int32_t>(A(r, y)) * cast<int32_t>(B(r % 4, x, r / 4));
    schedule_matmul(mm, r.x, /*tile_x=*/32, /*tile_y=*/16, /*tile_r=*/8);
    mm.in().compile_jit(amx_target);
}

// AMXTile allocated for a non-i32/f32 result. AMX always accumulates into
// 32-bit registers, so we reject. Triggers the user_assert in
// visit(Allocate).
void scenario_bad_result_type() {
    Buffer<int8_t> A(64, 64);
    Buffer<int8_t> B(4, 64, 16);
    Var x("x"), y("y");
    RDom r(0, 64, "r");

    Func mm("matmul_i16");
    mm(x, y) = cast<int16_t>(0);
    mm(x, y) += cast<int16_t>(A(r, y)) * cast<int16_t>(B(r % 4, x, r / 4));
    schedule_matmul(mm, r.x, 8, 8, 8);
    mm.in().compile_jit(amx_target);
}

// The most natural form of an int8 matmul: row-major LHS and RHS, no VNNI
// packing. AMX requires the RHS to be pre-packed as (4, cols, rows/4); a
// row-major (col, row) RHS isn't expressible as an AMX tile load, so we
// reject. Triggers the layout check in convert_to_matmul.
void scenario_naive_rhs() {
    Buffer<int8_t> A(64, 64);
    Buffer<int8_t> B(64, 64);
    Var x("x"), y("y");
    RDom r(0, 64, "r");

    Func mm("matmul_naive");
    mm(x, y) = cast<int32_t>(0);
    mm(x, y) += cast<int32_t>(A(r, y)) * cast<int32_t>(B(x, r));
    schedule_matmul(mm, r.x, 8, 8, 8);
    mm.in().compile_jit(amx_target);
}

// A gather-style matmul with an indirect row index — natural for sparse /
// pruned matmul, indirect attention, embedding lookups. The LHS load index
// goes through a table lookup, so the multiramp lift fails. Triggers the
// "loads indices are not affine" path in convert_to_matmul.
void scenario_indirect() {
    Buffer<int8_t> A(64, 64);
    Buffer<int8_t> B(4, 64, 16);
    Buffer<int32_t> row_indices(64);
    Var x("x"), y("y");
    RDom r(0, 64, "r");

    Func mm("matmul_indirect");
    mm(x, y) = cast<int32_t>(0);
    mm(x, y) +=
        cast<int32_t>(A(r, clamp(row_indices(y), 0, 10))) *
        cast<int32_t>(B(r % 4, x, r / 4));
    schedule_matmul(mm, r.x, 8, 8, 8);
    mm.in().compile_jit(amx_target);
}

// A 1D convolution of a 2D signal with per-row kernels, aggressively
// vectorized. Structurally a sum-of-widening-multiplies with a contiguous
// inner K, but the LHS depends on x, k, and y simultaneously (no broadcast
// dim) and so doesn't match the AMX matmul shape. Triggers the
// access-pattern / layout check.
void scenario_conv1d() {
    Buffer<int8_t> input(128, 128);
    Buffer<int8_t> kernels(4, 64, 8);
    Var x("x"), y("y");
    const int K = 32;
    RDom r(0, K, "r");

    Func conv("conv");
    conv(x, y) = cast<int32_t>(0);
    conv(x, y) +=
        cast<int32_t>(input(x + r, y)) *
        cast<int32_t>(kernels(r % 4, x, r / 4));
    schedule_matmul(conv, r.x, 8, 8, 8);
    conv.in().compile_jit(amx_target);
}

// A non-matmul value scheduled into an AMXTile allocation by mistake.
// Triggers the "no matrix multiply was found" assertion.
void scenario_no_matmul() {
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    Func f("f");
    f(x, y) = 0;
    f.compute_at(f.in(), xo)
        .store_in(MemoryType::AMXTile)
        .vectorize(x, 8)
        .vectorize(y, 8);

    f.in().tile(x, y, xo, yo, xi, yi, 8, 8).vectorize(xi).vectorize(yi);
    f.in().compile_jit(amx_target);
}

// A user wants i16 × i16 → i32, expecting AMX to widen-multiply 16-bit
// inputs the way it does 8-bit. AMX's tdpb*sd instructions only support
// 8-bit input lanes (and bf16 for floats); we reject anything else. The
// reduction inside is still a widening multiply, but with the wrong inner
// element width, so the type check fires.
void scenario_widening_16bit() {
    Buffer<int16_t> A(64, 64);
    Buffer<int16_t> B(4, 64, 16);
    Var x("x"), y("y");
    RDom r(0, 64, "r");

    Func mm("matmul_i16_input");
    mm(x, y) = cast<int32_t>(0);
    mm(x, y) += cast<int32_t>(A(r, y)) * cast<int32_t>(B(r % 4, x, r / 4));
    schedule_matmul(mm, r.x, 8, 8, 8);
    mm.in().compile_jit(amx_target);
}

// A user gives the same Func two update definitions that each store into
// the same AMXTile allocation but with different tile sizes (e.g. a fast
// path for the bulk of K and a smaller fallback). The matcher requires
// every matmul touching a given allocation to agree on tile dimensions.
void scenario_inconsistent_tiles() {
    Buffer<int8_t> A(64, 64), C(64, 64);
    Buffer<int8_t> B(4, 64, 16), D(4, 64, 16);
    Var x("x"), y("y");
    RDom r1(0, 64, "r1"), r2(0, 64, "r2");

    Func mm("matmul_two_updates");
    mm(x, y) = cast<int32_t>(0);
    mm(x, y) += cast<int32_t>(A(r1, y)) * cast<int32_t>(B(r1 % 4, x, r1 / 4));
    mm(x, y) += cast<int32_t>(C(r2, y)) * cast<int32_t>(D(r2 % 4, x, r2 / 4));

    Var rxi("rxi"), ryi("ryi");
    RVar rri("rri"), rro("rro");

    mm.compute_at(mm.in(), x).store_in(MemoryType::AMXTile);
    mm.update(0)
        .tile(x, y, rxi, ryi, 8, 8, TailStrategy::GuardWithIf)
        .split(r1.x, rro, rri, 8)
        .reorder(rri, rxi, ryi, rro, x, y)
        .atomic()
        .vectorize(rri)
        .vectorize(rxi)
        .vectorize(ryi);
    mm.update(1)
        .tile(x, y, rxi, ryi, 4, 4, TailStrategy::GuardWithIf)
        .split(r2.x, rro, rri, 8)
        .reorder(rri, rxi, ryi, rro, x, y)
        .atomic()
        .vectorize(rri)
        .vectorize(rxi)
        .vectorize(ryi);

    Var ixi("ixi"), iyi("iyi");
    mm.compute_at(mm.in(), x)
        .tile(x, y, ixi, iyi, 8, 8)
        .vectorize(ixi)
        .vectorize(iyi);

    Var mmxi("mmxi"), mmyi("mmyi");
    mm.in()
        .tile(x, y, mmxi, mmyi, 8, 8)
        .vectorize(mmxi)
        .vectorize(mmyi);

    mm.in().compile_jit(amx_target);
}

// A reduction inside an AMXTile that's a sum-of-something-else, not a
// vector_reduce_add of a widening multiply. Here we accumulate a
// non-multiplied value, which produces a Store whose RHS is not a
// vector-reduce-of-multiply.
void scenario_not_a_matmul_pattern() {
    Buffer<int32_t> A(64, 64);
    Var x("x"), y("y");
    RDom r(0, 64, "r");

    Func mm("not_matmul");
    mm(x, y) = cast<int32_t>(0);
    // Sum without a multiply.
    mm(x, y) += A(r, y);

    Var rxi("rxi"), ryi("ryi");
    RVar rri("rri"), rro("rro");
    mm.compute_at(mm.in(), x)
        .store_in(MemoryType::AMXTile)
        .update()
        .tile(x, y, rxi, ryi, 8, 8, TailStrategy::GuardWithIf)
        .split(r.x, rro, rri, 8)
        .reorder(rri, rxi, ryi, rro, x, y)
        .atomic()
        .vectorize(rri)
        .vectorize(rxi)
        .vectorize(ryi);

    Var ixi("ixi"), iyi("iyi");
    mm.compute_at(mm.in(), x)
        .tile(x, y, ixi, iyi, 8, 8)
        .vectorize(ixi)
        .vectorize(iyi);

    Var mmxi("mmxi"), mmyi("mmyi");
    mm.in()
        .tile(x, y, mmxi, mmyi, 8, 8)
        .vectorize(mmxi)
        .vectorize(mmyi);

    mm.in().compile_jit(amx_target);
}

// Multiplication of a matrix by a value. In theory we could materialize the
// value into memory (e.g. as a scaled identity matrix), but we don't, and the
// matcher rejects.
void scenario_matmul_by_constant() {
    Buffer<int8_t> A(64, 64);
    Var x("x"), y("y");
    RDom r(0, 64, "r");

    Func mm("matmul_by_constant");
    mm(x, y) = cast<int32_t>(0);
    mm(x, y) += cast<int32_t>(A(r, y)) * select(x == r, cast<int8_t>(3), cast<int8_t>(0));
    schedule_matmul(mm, r.x, 8, 8, 8);
    mm.in().compile_jit(amx_target);
}

}  // namespace

int main(int argc, char **argv) {
    if (!Halide::exceptions_enabled()) {
        printf("[SKIP] Halide was compiled without exceptions.\n");
        return 0;
    }

    int failures = 0;

    failures += !expect_user_error("too_large", scenario_too_large);
    failures += !expect_user_error("bad_result_type", scenario_bad_result_type);
    failures += !expect_user_error("naive_rhs", scenario_naive_rhs);
    failures += !expect_user_error("indirect", scenario_indirect);
    failures += !expect_user_error("conv1d", scenario_conv1d);
    failures += !expect_user_error("no_matmul", scenario_no_matmul);
    failures += !expect_user_error("widening_16bit", scenario_widening_16bit);
    failures += !expect_user_error("inconsistent_tiles", scenario_inconsistent_tiles);
    failures += !expect_user_error("not_a_matmul_pattern", scenario_not_a_matmul_pattern);
    failures += !expect_user_error("matmul_by_constant", scenario_matmul_by_constant);

    if (failures != 0) {
        printf("%d scenario(s) failed to produce a user-facing CompileError\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}

#else  // HALIDE_WITH_EXCEPTIONS

int main(int argc, char **argv) {
    printf("[SKIP] Halide was compiled without exceptions.\n");
    return 0;
}

#endif  // HALIDE_WITH_EXCEPTIONS
