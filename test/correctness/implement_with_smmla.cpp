// Real-world end-to-end validation of `implement_with`:
//   substitute in the ARM i8mm `smmla` instruction for a 2x2/K=8
//   quantized matmul tile.
//
// Why this is the right validation. Apple M3 Max has FEAT_I8MM
// (smmla / ummla); a single `smmla v0.4s, v1.16b, v2.16b` computes a
// 2x2 int32 output tile from a 2x8 x 8x2 int8 matmul in ONE
// instruction. Halide has zero mentions of SMMLA / i8mm anywhere
// in src/ or its test suite --- no FindIntrinsics rule produces
// smmla; no built-in pattern recognises it. Compiled with default
// scheduling, the matmul tile in this test produces zero smmla
// (and zero sdot) mnemonics in the lowered assembly. That makes
// this kernel the smallest demonstrable gap between Halide-as-is
// and what the hardware can do, which `implement_with` is meant
// to close.
//
// What this test does.
//   1. Defines an `Instruction` whose spec is a 2x2/K=8 int8 -> int32
//      matmul tile (the algebraic shape SMMLA computes), and whose
//      emit returns a Halide Stmt invoking the LLVM intrinsic
//      `llvm.aarch64.neon.smmla.v4i32.v16i8`.
//   2. Builds a user pipeline with the same algebra plus
//      `out.update().implement_with(...)`.
//   3. Compiles to assembly. Asserts that `smmla` appears in the
//      assembly (and the baseline pipeline without implement_with
//      does NOT emit smmla).
//   4. JIT-compiles both pipelines, runs them on random int8 inputs,
//      and confirms the substituted output matches the baseline
//      output bit-for-bit (numerical correctness).
//
// Skipped if the host target lacks ARMv86a/i8mm or is not ARM at all.

#include "Halide.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace Halide;

namespace {

// LLVM intrinsic name for ARM SMMLA. Identified by
// /opt/homebrew/opt/llvm/bin/clang -O2 -S -emit-llvm with arm_neon.h's
// `vmmlaq_s32`.
const char *const kSmmlaIntrinsic = "llvm.aarch64.neon.smmla.v4i32.v16i8";

// The instruction declaration. Spec models a 2x2/K=8 int8 -> int32
// matmul tile (SMMLA's exact algebraic shape). Spec input Funcs are
// `a` and `b`, both 2x8 int8 row-major; spec output `out` is 2x2
// int32 with j innermost so its linear layout matches SMMLA's 4-lane
// output ([a0.b0, a0.b1, a1.b0, a1.b1]).
//
// The spec input/output storage orders are forced explicitly so the
// canonical-form Loads/Stores in the spec match those of a user
// pipeline that picks the same layout.
Instruction make_smmla_instruction() {
    return Instruction::declare("smmla_2x2_k8")
        .spec([]() -> Pipeline {
            Var i("i"), j("j");
            RDom k(0, 8, "k");
            Func a(Int(8), 2, "a"), b(Int(8), 2, "b"), out("out");
            out(i, j) = cast<int32_t>(0);
            out(i, j) +=
                cast<int32_t>(a(i, k.x)) * cast<int32_t>(b(j, k.x));

            // The auto-stubbed Funcs `a` and `b` pick up their arg
            // names from the call site (Phase 4 §3.4 wart). Since k
            // is an RVar, the call site name comes out as something
            // like "k$x". Use the actual arg names for reorder_storage
            // / bound.
            const std::string ak = a.args()[1].name();
            const std::string bk = b.args()[1].name();
            // Force row-major (k contiguous) storage on the spec
            // inputs to match the user's set_stride(8, 1) on
            // ImageParam, so the matched canonical-form Loads have
            // identical index expressions on both sides.
            a.reorder_storage(Var(ak), Var("i"));
            b.reorder_storage(Var(bk), Var("j"));
            a.bound(Var("i"), 0, 2).bound(Var(ak), 0, 8);
            b.bound(Var("j"), 0, 2).bound(Var(bk), 0, 8);
            // Output: default storage (i innermost). The emit
            // handles the SMMLA-output -> Halide-layout permutation
            // via a Shuffle.
            out.bound(i, 0, 2).bound(j, 0, 2);
            return Pipeline({out});
        })
        .require({Target::ARMv86a})
        .emit([](const MatchContext &ctx) -> Internal::Stmt {
            const std::string &a_name = ctx.input("a");
            const std::string &b_name = ctx.input("b");
            const std::string &out_name = ctx.output("out");

            // Load 16 contiguous int8s from each input. With the spec's
            // row-major (k-innermost) storage these are
            // [a(0,0..7), a(1,0..7)] and similarly for b.
            Expr a_vec = Internal::Load::make(
                Int(8, 16), a_name,
                Internal::Ramp::make(Expr(0), Expr(1), 16),
                Buffer<>(), Parameter(), Internal::const_true(16),
                Internal::ModulusRemainder());
            Expr b_vec = Internal::Load::make(
                Int(8, 16), b_name,
                Internal::Ramp::make(Expr(0), Expr(1), 16),
                Buffer<>(), Parameter(), Internal::const_true(16),
                Internal::ModulusRemainder());

            // Zero accumulator (4-lane int32).
            Expr acc = Internal::Broadcast::make(
                Internal::make_const(Int(32), 0), 4);

            // SMMLA call. Use Call::PureExtern --- it bypasses
            // CodeGen_LLVM's intrinsic-lowering dispatch (which is
            // for Halide-internal intrinsic names) and routes through
            // a special llvm.* path that preserves vector arg types.
            // The 4-int32 result is laid out as
            //   result[0] = out(0,0)   result[1] = out(0,1)
            //   result[2] = out(1,0)   result[3] = out(1,1)
            // i.e. j is innermost in SMMLA's result.
            Expr smmla_result = Internal::Call::make(
                Int(32, 4), kSmmlaIntrinsic,
                {acc, a_vec, b_vec},
                Internal::Call::PureExtern);

            // The user-side output buffer uses Halide's default
            // i-innermost layout (stride.0=1, stride.1=2). Permute
            // SMMLA's j-innermost lanes into the buffer's order:
            //   buffer[0] = out(0,0) = result[0]
            //   buffer[1] = out(1,0) = result[2]
            //   buffer[2] = out(0,1) = result[1]
            //   buffer[3] = out(1,1) = result[3]
            // i.e. {0, 2, 1, 3}.
            Expr permuted = Internal::Shuffle::make(
                {smmla_result}, {0, 2, 1, 3});

            return Internal::Store::make(
                out_name, permuted,
                Internal::Ramp::make(Expr(0), Expr(1), 4),
                Parameter(), Internal::const_true(4),
                Internal::ModulusRemainder());
        })
        .build();
}

Target tile_target() {
    // M3 Max: arm-64 with ARMv86a (includes mandatory i8mm) and
    // ARMDotProd. Use the JIT target as a base so we can JIT later.
    return get_jit_target_from_environment()
        .with_feature(Target::ARMv86a)
        .with_feature(Target::ARMDotProd);
}

// User-side pipeline producing the same 2x2/K=8 tile from ImageParam
// inputs. Storage layout matches the spec exactly (row-major inputs,
// j-innermost output) so canonical-form matching has the best chance
// of succeeding. `with_directive` controls whether the implement_with
// directive is attached.
Pipeline build_user_pipeline(ImageParam &A, ImageParam &B, Func &out,
                             const Instruction &instr,
                             bool with_directive) {
    // Inputs: row-major (i outer in storage, k contiguous).
    A.dim(0).set_bounds(0, 2).set_stride(8);
    A.dim(1).set_bounds(0, 8).set_stride(1);
    B.dim(0).set_bounds(0, 2).set_stride(8);
    B.dim(1).set_bounds(0, 8).set_stride(1);

    Var i("i"), j("j");
    RDom k(0, 8, "k");
    out(i, j) = cast<int32_t>(0);
    out(i, j) += cast<int32_t>(A(i, k.x)) * cast<int32_t>(B(j, k.x));
    // Use the default storage layout (i innermost, stride.0=1,
    // stride.1=2). The emit handles the SMMLA-output permutation via
    // a 4-lane Shuffle so the contiguous Store lands in the right
    // place.
    if (with_directive) {
        // Do NOT call out.bound() here: a duplicate (user + spec-
        // transferred) bound prevents allocation_bounds_inference
        // from pinning out.min.0 / out.min.1 to 0, which leaves a
        // (- out.min.N) subtraction in the user-side Store indices
        // that the spec side does not have. Soft-failure semantics
        // would then swallow the match. The spec's transferred
        // bound is sufficient on its own.
        out.update().implement_with(j, instr);
    } else {
        // Baseline: bound to the same range so the two pipelines are
        // strictly comparable.
        out.bound(i, 0, 2).bound(j, 0, 2);
    }
    return Pipeline(out);
}

bool file_contains(const std::string &path, const std::string &needle) {
    std::ifstream f(path);
    std::string line;
    while (getline(f, line)) {
        if (line.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Compile both pipelines to assembly and confirm:
//   - the baseline (no implement_with) does NOT emit smmla,
//   - the substituted pipeline DOES emit smmla.
void check_assembly_emits_smmla() {
    Target t = tile_target();
    if (t.arch != Target::ARM ||
        !(t.has_feature(Target::ARMv86a))) {
        printf("[SKIP] check_assembly_emits_smmla: target lacks ARMv86a "
               "(host=%s)\n",
               t.to_string().c_str());
        return;
    }

    Instruction instr = make_smmla_instruction();

    // Baseline pipeline.
    {
        ImageParam A(Int(8), 2, "A"), B(Int(8), 2, "B");
        Func out("out_baseline");
        Pipeline pipe = build_user_pipeline(A, B, out, instr,
                                            /*with_directive=*/false);
        pipe.compile_to_assembly("/tmp/iw_smmla_baseline.s", {A, B},
                                 "out_baseline", t);
    }
    if (file_contains("/tmp/iw_smmla_baseline.s", "smmla")) {
        fprintf(stderr,
                "check_assembly_emits_smmla: baseline (no implement_with) "
                "unexpectedly produced an smmla mnemonic. The gap this "
                "test demonstrates has closed --- either Halide gained a "
                "FindIntrinsics rule for smmla, or this build picks up a "
                "different LLVM. Either way, this test needs updating.\n");
        std::exit(1);
    }

    // implement_with pipeline.
    {
        ImageParam A(Int(8), 2, "A"), B(Int(8), 2, "B");
        Func out("out_sub");
        Pipeline pipe = build_user_pipeline(A, B, out, instr,
                                            /*with_directive=*/true);
        pipe.compile_to_assembly("/tmp/iw_smmla_sub.s", {A, B},
                                 "out_sub", t);
    }
    if (!file_contains("/tmp/iw_smmla_sub.s", "smmla")) {
        fprintf(stderr,
                "check_assembly_emits_smmla: implement_with pipeline did "
                "NOT produce an smmla mnemonic. Either the matcher fell "
                "through to soft-failure (check stderr for the warning) "
                "or the emit Stmt did not survive lowering / codegen.\n");
        // Dump the assembly to aid debug.
        std::ifstream f("/tmp/iw_smmla_sub.s");
        std::string line;
        int line_no = 0;
        while (getline(f, line) && line_no < 60) {
            ++line_no;
            fprintf(stderr, "  %4d: %s\n", line_no, line.c_str());
        }
        std::exit(1);
    }
}

// Compute the 2x2/K=8 tile naively in C++ for the correctness check.
void reference_matmul(const Halide::Buffer<int8_t> &A,
                      const Halide::Buffer<int8_t> &B,
                      Halide::Buffer<int32_t> &out) {
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            int32_t acc = 0;
            for (int k = 0; k < 8; ++k) {
                acc += static_cast<int32_t>(A(i, k)) *
                       static_cast<int32_t>(B(j, k));
            }
            out(i, j) = acc;
        }
    }
}

// Create a 2x8 int8 buffer with row-major storage (dim 0 stride 8,
// dim 1 stride 1) so it matches the user pipeline's set_stride
// constraints.
Halide::Buffer<int8_t> make_row_major_2x8(std::vector<int8_t> &data) {
    data.assign(16, 0);
    halide_dimension_t dims[2] = {
        {0, 2, 8},
        {0, 8, 1},
    };
    return Halide::Buffer<int8_t>(data.data(), 2, dims);
}

// Output: Halide default layout (i innermost, dim 0 stride 1, dim 1
// stride 2).
Halide::Buffer<int32_t> make_default_2x2(std::vector<int32_t> &data) {
    data.assign(4, 0);
    halide_dimension_t dims[2] = {
        {0, 2, 1},
        {0, 2, 2},
    };
    return Halide::Buffer<int32_t>(data.data(), 2, dims);
}

void check_numerical_correctness() {
    Target t = tile_target();
    if (t.arch != Target::ARM ||
        !(t.has_feature(Target::ARMv86a))) {
        printf("[SKIP] check_numerical_correctness: target lacks ARMv86a\n");
        return;
    }

    Instruction instr = make_smmla_instruction();

    std::vector<int8_t> a_data, b_data;
    Halide::Buffer<int8_t> a_buf = make_row_major_2x8(a_data);
    Halide::Buffer<int8_t> b_buf = make_row_major_2x8(b_data);
    std::mt19937 rng(0xc0ffee);
    std::uniform_int_distribution<int> dist(-100, 100);
    for (int i = 0; i < 2; ++i) {
        for (int k = 0; k < 8; ++k) {
            a_buf(i, k) = static_cast<int8_t>(dist(rng));
        }
    }
    for (int j = 0; j < 2; ++j) {
        for (int k = 0; k < 8; ++k) {
            b_buf(j, k) = static_cast<int8_t>(dist(rng));
        }
    }

    Halide::Buffer<int32_t> ref(2, 2);
    reference_matmul(a_buf, b_buf, ref);

    auto run = [&](bool with_directive,
                   Halide::Buffer<int32_t> &out_buf) {
        ImageParam A(Int(8), 2, "A"), B(Int(8), 2, "B");
        Func out("out_corr");
        Pipeline pipe = build_user_pipeline(A, B, out, instr, with_directive);
        A.set(a_buf);
        B.set(b_buf);
        pipe.realize(out_buf, t);
    };

    std::vector<int32_t> base_data, sub_data;
    Halide::Buffer<int32_t> base_out = make_default_2x2(base_data);
    Halide::Buffer<int32_t> sub_out = make_default_2x2(sub_data);
    run(/*with_directive=*/false, base_out);
    run(/*with_directive=*/true, sub_out);

    bool ok = true;
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            if (base_out(i, j) != ref(i, j) ||
                sub_out(i, j) != ref(i, j)) {
                ok = false;
                fprintf(stderr,
                        "check_numerical_correctness: mismatch at (i=%d, j=%d): "
                        "ref=%d base=%d sub=%d\n",
                        i, j, ref(i, j), base_out(i, j), sub_out(i, j));
            }
        }
    }
    if (!ok) std::exit(1);
}

}  // namespace

int main(int, char **) {
    try {
        check_assembly_emits_smmla();
        check_numerical_correctness();
    } catch (const Halide::CompileError &e) {
        fprintf(stderr, "Halide::CompileError: %s\n", e.what());
        return 1;
    } catch (const Halide::Error &e) {
        fprintf(stderr, "Halide::Error: %s\n", e.what());
        return 1;
    } catch (const std::exception &e) {
        fprintf(stderr, "std::exception: %s\n", e.what());
        return 1;
    }
    printf("Success!\n");
    return 0;
}
