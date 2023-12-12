#include "CodeGen_Internal.h"
#include "CodeGen_Posix.h"
#include "ConciseCasts.h"
#include "Debug.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "LLVM_Headers.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::pair;
using std::string;
using std::vector;

using namespace Halide::ConciseCasts;
using namespace llvm;

#if defined(WITH_X86)

namespace {

// Populate feature flags in a target according to those implied by
// existing flags, so that instruction patterns can just check for the
// oldest feature flag that supports an instruction.
Target complete_x86_target(Target t) {
    if (t.has_feature(Target::AVX512_SapphireRapids)) {
        t.set_feature(Target::AVX512_Zen4);
    }
    if (t.has_feature(Target::AVX512_Zen4)) {
        t.set_feature(Target::AVX512_Cannonlake);
    }
    if (t.has_feature(Target::AVX512_Cannonlake)) {
        t.set_feature(Target::AVX512_Skylake);
    }
    if (t.has_feature(Target::AVX512_Cannonlake) ||
        t.has_feature(Target::AVX512_Skylake) ||
        t.has_feature(Target::AVX512_KNL)) {
        t.set_feature(Target::AVX512);
    }
    if (t.has_feature(Target::AVX512)) {
        t.set_feature(Target::AVX2);
    }
    if (t.has_feature(Target::AVX2)) {
        t.set_feature(Target::AVX);
        // All AVX2-enabled architectures have F16C and FMA
        t.set_feature(Target::F16C);
        t.set_feature(Target::FMA);
    }
    if (t.has_feature(Target::AVX)) {
        t.set_feature(Target::SSE41);
    }
    return t;
}

/** A code generator that emits x86 code from a given Halide stmt. */
class CodeGen_X86 : public CodeGen_Posix {
public:
    /** Create an x86 code generator. Processor features can be
     * enabled using the appropriate flags in the target struct. */
    CodeGen_X86(Target);

protected:
    string mcpu_target() const override;
    string mcpu_tune() const override;
    string mattrs() const override;
    bool use_soft_float_abi() const override;
    int native_vector_bits() const override;

    int vector_lanes_for_slice(const Type &t) const;

    using CodeGen_Posix::visit;

    void init_module() override;

    /** Nodes for which we want to emit specific sse/avx intrinsics */
    // @{
    void visit(const Add *) override;
    void visit(const Sub *) override;
    void visit(const Cast *) override;
    void visit(const Call *) override;
    void visit(const GT *) override;
    void visit(const LT *) override;
    void visit(const LE *) override;
    void visit(const GE *) override;
    void visit(const EQ *) override;
    void visit(const NE *) override;
    void visit(const Select *) override;
    void visit(const Allocate *) override;
    void visit(const Load *) override;
    void visit(const Store *) override;
    void codegen_vector_reduce(const VectorReduce *, const Expr &init) override;
    // @}

private:
    Scope<MemoryType> mem_type;
};

CodeGen_X86::CodeGen_X86(Target t)
    : CodeGen_Posix(complete_x86_target(t)) {
}

const int max_intrinsic_args = 6;

struct x86Intrinsic {
    const char *intrin_name;
    halide_type_t ret_type;
    const char *name;
    halide_type_t arg_types[max_intrinsic_args];
    Target::Feature feature = Target::FeatureEnd;
    uint32_t flags = 0;
    enum Options {
        AccessesMemory = 1 << 0,
    };
};

// clang-format off
const x86Intrinsic intrinsic_defs[] = {
    // AVX2/SSSE3 LLVM intrinsics for pabs fail in JIT. The integer wrappers
    // just call `llvm.abs` (which requires a second argument).
    // AVX512BW's pabs instructions aren't directly exposed by LLVM.
    {"abs_i8x64", UInt(8, 64), "abs", {Int(8, 64)}, Target::AVX512_Skylake},
    {"abs_i16x32", UInt(16, 32), "abs", {Int(16, 32)}, Target::AVX512_Skylake},
    {"abs_i32x16", UInt(32, 16), "abs", {Int(32, 16)}, Target::AVX512_Skylake},
    {"abs_i8x32", UInt(8, 32), "abs", {Int(8, 32)}, Target::AVX2},
    {"abs_i16x16", UInt(16, 16), "abs", {Int(16, 16)}, Target::AVX2},
    {"abs_i32x8", UInt(32, 8), "abs", {Int(32, 8)}, Target::AVX2},
    {"abs_f32x8", Float(32, 8), "abs", {Float(32, 8)}, Target::AVX2},
    {"abs_i8x16", UInt(8, 16), "abs", {Int(8, 16)}, Target::SSE41},
    {"abs_i16x8", UInt(16, 8), "abs", {Int(16, 8)}, Target::SSE41},
    {"abs_i32x4", UInt(32, 4), "abs", {Int(32, 4)}, Target::SSE41},
    {"abs_f32x4", Float(32, 4), "abs", {Float(32, 4)}},

    {"round_f32x4", Float(32, 4), "round", {Float(32, 4)}, Target::SSE41},
    {"round_f64x2", Float(64, 2), "round", {Float(64, 2)}, Target::SSE41},
    {"round_f32x8", Float(32, 8), "round", {Float(32, 8)}, Target::AVX},
    {"round_f64x4", Float(64, 4), "round", {Float(64, 4)}, Target::AVX},

    {"llvm.sadd.sat.v64i8", Int(8, 64), "saturating_add", {Int(8, 64), Int(8, 64)}, Target::AVX512_Skylake},
    {"llvm.sadd.sat.v32i8", Int(8, 32), "saturating_add", {Int(8, 32), Int(8, 32)}, Target::AVX2},
    {"llvm.sadd.sat.v16i8", Int(8, 16), "saturating_add", {Int(8, 16), Int(8, 16)}},
    {"llvm.sadd.sat.v8i8", Int(8, 8), "saturating_add", {Int(8, 8), Int(8, 8)}},
    {"llvm.ssub.sat.v64i8", Int(8, 64), "saturating_sub", {Int(8, 64), Int(8, 64)}, Target::AVX512_Skylake},
    {"llvm.ssub.sat.v32i8", Int(8, 32), "saturating_sub", {Int(8, 32), Int(8, 32)}, Target::AVX2},
    {"llvm.ssub.sat.v16i8", Int(8, 16), "saturating_sub", {Int(8, 16), Int(8, 16)}},
    {"llvm.ssub.sat.v8i8", Int(8, 8), "saturating_sub", {Int(8, 8), Int(8, 8)}},

    {"llvm.sadd.sat.v32i16", Int(16, 32), "saturating_add", {Int(16, 32), Int(16, 32)}, Target::AVX512_Skylake},
    {"llvm.sadd.sat.v16i16", Int(16, 16), "saturating_add", {Int(16, 16), Int(16, 16)}, Target::AVX2},
    {"llvm.sadd.sat.v8i16", Int(16, 8), "saturating_add", {Int(16, 8), Int(16, 8)}},
    {"llvm.ssub.sat.v32i16", Int(16, 32), "saturating_sub", {Int(16, 32), Int(16, 32)}, Target::AVX512_Skylake},
    {"llvm.ssub.sat.v16i16", Int(16, 16), "saturating_sub", {Int(16, 16), Int(16, 16)}, Target::AVX2},
    {"llvm.ssub.sat.v8i16", Int(16, 8), "saturating_sub", {Int(16, 8), Int(16, 8)}},

    // Sum of absolute differences
    {"llvm.x86.sse2.psad.bw", UInt(64, 2), "sum_of_absolute_differences", {UInt(8, 16), UInt(8, 16)}},
    {"llvm.x86.avx2.psad.bw", UInt(64, 4), "sum_of_absolute_differences", {UInt(8, 32), UInt(8, 32)}, Target::AVX2},
    {"llvm.x86.avx512.psad.bw.512", UInt(64, 8), "sum_of_absolute_differences", {UInt(8, 64), UInt(8, 64)}, Target::AVX512_Skylake},

    // Some of the instructions referred to below only appear with
    // AVX2, but LLVM generates better AVX code if you give it
    // full 256-bit vectors and let it do the slicing up into
    // individual instructions itself. This is why we use
    // Target::AVX instead of Target::AVX2 as the feature flag
    // requirement.
    // TODO: Just use llvm.*add/*sub.sat, and verify the above comment?
    {"llvm.uadd.sat.v64i8", UInt(8, 64), "saturating_add", {UInt(8, 64), UInt(8, 64)}, Target::AVX512_Skylake},
    {"paddusbx32", UInt(8, 32), "saturating_add", {UInt(8, 32), UInt(8, 32)}, Target::AVX},
    {"paddusbx16", UInt(8, 16), "saturating_add", {UInt(8, 16), UInt(8, 16)}},
    {"llvm.usub.sat.v64i8", UInt(8, 64), "saturating_sub", {UInt(8, 64), UInt(8, 64)}, Target::AVX512_Skylake},
    {"psubusbx32", UInt(8, 32), "saturating_sub", {UInt(8, 32), UInt(8, 32)}, Target::AVX},
    {"psubusbx16", UInt(8, 16), "saturating_sub", {UInt(8, 16), UInt(8, 16)}},

    {"llvm.uadd.sat.v32i16", UInt(16, 32), "saturating_add", {UInt(16, 32), UInt(16, 32)}, Target::AVX512_Skylake},
    {"padduswx16", UInt(16, 16), "saturating_add", {UInt(16, 16), UInt(16, 16)}, Target::AVX},
    {"padduswx8", UInt(16, 8), "saturating_add", {UInt(16, 8), UInt(16, 8)}},
    {"llvm.usub.sat.v32i16", UInt(16, 32), "saturating_sub", {UInt(16, 32), UInt(16, 32)}, Target::AVX512_Skylake},
    {"psubuswx16", UInt(16, 16), "saturating_sub", {UInt(16, 16), UInt(16, 16)}, Target::AVX},
    {"psubuswx8", UInt(16, 8), "saturating_sub", {UInt(16, 8), UInt(16, 8)}},

    {"llvm.x86.avx512.pavg.b.512", UInt(8, 64), "rounding_halving_add", {UInt(8, 64), UInt(8, 64)}, Target::AVX512_Skylake},
    {"llvm.x86.avx2.pavg.b", UInt(8, 32), "rounding_halving_add", {UInt(8, 32), UInt(8, 32)}, Target::AVX2},
    {"llvm.x86.sse2.pavg.b", UInt(8, 16), "rounding_halving_add", {UInt(8, 16), UInt(8, 16)}},
    {"llvm.x86.avx512.pavg.w.512", UInt(16, 32), "rounding_halving_add", {UInt(16, 32), UInt(16, 32)}, Target::AVX512_Skylake},
    {"llvm.x86.avx2.pavg.w", UInt(16, 16), "rounding_halving_add", {UInt(16, 16), UInt(16, 16)}, Target::AVX2},
    {"llvm.x86.sse2.pavg.w", UInt(16, 8), "rounding_halving_add", {UInt(16, 8), UInt(16, 8)}},

    {"packssdwx16", Int(16, 16), "saturating_narrow", {Int(32, 16)}, Target::AVX2},
    {"packssdwx8", Int(16, 8), "saturating_narrow", {Int(32, 8)}},
    {"packsswbx32", Int(8, 32), "saturating_narrow", {Int(16, 32)}, Target::AVX2},
    {"packsswbx16", Int(8, 16), "saturating_narrow", {Int(16, 16)}},
    {"packusdwx16", UInt(16, 16), "saturating_narrow", {Int(32, 16)}, Target::AVX2},
    {"packusdwx8", UInt(16, 8), "saturating_narrow", {Int(32, 8)}, Target::SSE41},
    {"packuswbx32", UInt(8, 32), "saturating_narrow", {Int(16, 32)}, Target::AVX2},
    {"packuswbx16", UInt(8, 16), "saturating_narrow", {Int(16, 16)}},

    // Widening multiplies that use (v)pmaddwd
    {"wmul_pmaddwd_avx512", Int(32, 16), "widening_mul", {Int(16, 16), Int(16, 16)}, Target::AVX512_Skylake},
    {"wmul_pmaddwd_avx2", Int(32, 8), "widening_mul", {Int(16, 8), Int(16, 8)}, Target::AVX2},
    {"wmul_pmaddwd_sse2", Int(32, 4), "widening_mul", {Int(16, 4), Int(16, 4)}},

    // Multiply keep high half
    {"llvm.x86.avx512.pmulh.w.512", Int(16, 32), "pmulh", {Int(16, 32), Int(16, 32)}, Target::AVX512_Skylake},
    {"llvm.x86.avx2.pmulh.w", Int(16, 16), "pmulh", {Int(16, 16), Int(16, 16)}, Target::AVX2},
    {"llvm.x86.avx512.pmulhu.w.512", UInt(16, 32), "pmulh", {UInt(16, 32), UInt(16, 32)}, Target::AVX512_Skylake},
    {"llvm.x86.avx2.pmulhu.w", UInt(16, 16), "pmulh", {UInt(16, 16), UInt(16, 16)}, Target::AVX2},
    {"llvm.x86.avx512.pmul.hr.sw.512", Int(16, 32), "pmulhrs", {Int(16, 32), Int(16, 32)}, Target::AVX512_Skylake},
    {"llvm.x86.avx2.pmul.hr.sw", Int(16, 16), "pmulhrs", {Int(16, 16), Int(16, 16)}, Target::AVX2},
    {"llvm.x86.sse2.pmulh.w", Int(16, 8), "pmulh", {Int(16, 8), Int(16, 8)}},
    {"llvm.x86.sse2.pmulhu.w", UInt(16, 8), "pmulh", {UInt(16, 8), UInt(16, 8)}},
    {"llvm.x86.ssse3.pmul.hr.sw.128", Int(16, 8), "pmulhrs", {Int(16, 8), Int(16, 8)}, Target::SSE41},

    // As of LLVM main September 5 2023, LLVM only has partial handling of
    // bfloat16. The below rules will match fine for simple examples, but bfloat
    // conversion will get folded through any nearby shuffles and cause
    // unimplemented errors in llvm's x86 instruction selection for the shuffle
    // node. Disabling them for now. See https://github.com/halide/Halide/issues/7219
    /*
    // Convert FP32 to BF16
    {"vcvtne2ps2bf16x32", BFloat(16, 32), "f32_to_bf16", {Float(32, 32)}, Target::AVX512_Zen4},
    {"llvm.x86.avx512bf16.cvtneps2bf16.512", BFloat(16, 16), "f32_to_bf16", {Float(32, 16)}, Target::AVX512_Zen4},
    {"llvm.x86.avx512bf16.cvtneps2bf16.256", BFloat(16, 8), "f32_to_bf16", {Float(32, 8)}, Target::AVX512_Zen4},
    // LLVM does not provide an unmasked 128bit cvtneps2bf16 intrinsic, so provide a wrapper around the masked version.
    {"vcvtneps2bf16x4", BFloat(16, 4), "f32_to_bf16", {Float(32, 4)}, Target::AVX512_Zen4},
    */

    // 2-way dot products
    {"llvm.x86.avx2.pmadd.ub.sw", Int(16, 16), "saturating_dot_product", {UInt(8, 32), Int(8, 32)}, Target::AVX2},
    {"llvm.x86.ssse3.pmadd.ub.sw.128", Int(16, 8), "saturating_dot_product", {UInt(8, 16), Int(8, 16)}, Target::SSE41},

    // Horizontal widening adds using 2-way dot products.
    {"hadd_pmadd_u8_avx512", UInt(16, 32), "horizontal_widening_add", {UInt(8, 64)}, Target::AVX512_Skylake},
    {"hadd_pmadd_u8_avx512", Int(16, 32), "horizontal_widening_add", {UInt(8, 64)}, Target::AVX512_Skylake},
    {"hadd_pmadd_i8_avx512", Int(16, 32), "horizontal_widening_add", {Int(8, 64)}, Target::AVX512_Skylake},
    {"hadd_pmadd_u8_avx2", UInt(16, 16), "horizontal_widening_add", {UInt(8, 32)}, Target::AVX2},
    {"hadd_pmadd_u8_avx2", Int(16, 16), "horizontal_widening_add", {UInt(8, 32)}, Target::AVX2},
    {"hadd_pmadd_i8_avx2", Int(16, 16), "horizontal_widening_add", {Int(8, 32)}, Target::AVX2},
    {"hadd_pmadd_u8_sse3", UInt(16, 8), "horizontal_widening_add", {UInt(8, 16)}, Target::SSE41},
    {"hadd_pmadd_u8_sse3", Int(16, 8), "horizontal_widening_add", {UInt(8, 16)}, Target::SSE41},
    {"hadd_pmadd_i8_sse3", Int(16, 8), "horizontal_widening_add", {Int(8, 16)}, Target::SSE41},

    {"hadd_pmadd_i16_avx512", Int(32, 16), "horizontal_widening_add", {Int(16, 32)}, Target::AVX512_Skylake},
    {"hadd_pmadd_i16_avx2", Int(32, 8), "horizontal_widening_add", {Int(16, 16)}, Target::AVX2},
    {"hadd_pmadd_i16_sse2", Int(32, 4), "horizontal_widening_add", {Int(16, 8)}},

    {"llvm.x86.avx512.pmaddw.d.512", Int(32, 16), "dot_product", {Int(16, 32), Int(16, 32)}, Target::AVX512_Skylake},
    {"llvm.x86.avx2.pmadd.wd", Int(32, 8), "dot_product", {Int(16, 16), Int(16, 16)}, Target::AVX2},
    {"llvm.x86.sse2.pmadd.wd", Int(32, 4), "dot_product", {Int(16, 8), Int(16, 8)}},

    // 4-way dot product vector reduction
    // The LLVM intrinsics combine the bf16 pairs into i32, so provide a wrapper to correctly call the intrinsic.
    {"dpbf16psx16", Float(32, 16), "dot_product", {Float(32, 16), BFloat(16, 32), BFloat(16, 32)}, Target::AVX512_Zen4},
    {"dpbf16psx8", Float(32, 8), "dot_product", {Float(32, 8), BFloat(16, 16), BFloat(16, 16)}, Target::AVX512_SapphireRapids},
    {"dpbf16psx4", Float(32, 4), "dot_product", {Float(32, 4), BFloat(16, 8), BFloat(16, 8)}, Target::AVX512_SapphireRapids},

    {"dpbusdx16", Int(32, 16), "dot_product", {Int(32, 16), UInt(8, 64), Int(8, 64)}, Target::AVX512_Zen4},
    {"dpbusdx8", Int(32, 8), "dot_product", {Int(32, 8), UInt(8, 32), Int(8, 32)}, Target::AVX512_SapphireRapids},
    {"dpbusdx4", Int(32, 4), "dot_product", {Int(32, 4), UInt(8, 16), Int(8, 16)}, Target::AVX512_SapphireRapids},

    {"dpwssdx16", Int(32, 16), "dot_product", {Int(32, 16), Int(16, 32), Int(16, 32)}, Target::AVX512_Zen4},
    {"dpwssdx8", Int(32, 8), "dot_product", {Int(32, 8), Int(16, 16), Int(16, 16)}, Target::AVX512_SapphireRapids},
    {"dpwssdx4", Int(32, 4), "dot_product", {Int(32, 4), Int(16, 8), Int(16, 8)}, Target::AVX512_SapphireRapids},

    {"dpbusdsx16", Int(32, 16), "saturating_dot_product", {Int(32, 16), UInt(8, 64), Int(8, 64)}, Target::AVX512_Zen4},
    {"dpbusdsx8", Int(32, 8), "saturating_dot_product", {Int(32, 8), UInt(8, 32), Int(8, 32)}, Target::AVX512_SapphireRapids},
    {"dpbusdsx4", Int(32, 4), "saturating_dot_product", {Int(32, 4), UInt(8, 16), Int(8, 16)}, Target::AVX512_SapphireRapids},

    {"dpwssdsx16", Int(32, 16), "saturating_dot_product", {Int(32, 16), Int(16, 32), Int(16, 32)}, Target::AVX512_Zen4},
    {"dpwssdsx8", Int(32, 8), "saturating_dot_product", {Int(32, 8), Int(16, 16), Int(16, 16)}, Target::AVX512_SapphireRapids},
    {"dpwssdsx4", Int(32, 4), "saturating_dot_product", {Int(32, 4), Int(16, 8), Int(16, 8)}, Target::AVX512_SapphireRapids},

    {"tileloadd64_i8", Int(8, 1024), "tile_load", {Int(16), Int(16), Handle(), Int(64), Int(64)}, Target::AVX512_SapphireRapids, x86Intrinsic::AccessesMemory},
    {"tileloadd64_i8", UInt(8, 1024), "tile_load", {Int(16), Int(16), Handle(), Int(64), Int(64)}, Target::AVX512_SapphireRapids, x86Intrinsic::AccessesMemory},
    {"tileloadd64_bf16", BFloat(16, 512), "tile_load", {Int(16), Int(16), Handle(), Int(64), Int(64)}, Target::AVX512_SapphireRapids, x86Intrinsic::AccessesMemory},
    {"tdpbssd", Int(32, 256), "tile_matmul", {Int(16), Int(16), Int(16), Int(32, 256), Int(8, 1024), Int(8, 1024)},  Target::AVX512_SapphireRapids},
    {"tdpbsud", Int(32, 256), "tile_matmul", {Int(16), Int(16), Int(16), Int(32, 256), Int(8, 1024), UInt(8, 1024)}, Target::AVX512_SapphireRapids},
    {"tdpbusd", Int(32, 256), "tile_matmul", {Int(16), Int(16), Int(16), Int(32, 256), UInt(8, 1024), Int(8, 1024)}, Target::AVX512_SapphireRapids},
    {"tdpbuud", Int(32, 256), "tile_matmul", {Int(16), Int(16), Int(16), Int(32, 256), UInt(8, 1024), UInt(8, 1024)}, Target::AVX512_SapphireRapids},
    {"tdpbf16ps", Float(32, 256), "tile_matmul", {Int(16), Int(16), Int(16), Float(32, 256), BFloat(16, 512), BFloat(16, 512)}, Target::AVX512_SapphireRapids},
    {"tilezero_i32", Int(32, 256), "tile_zero", {Int(16), Int(16)},  Target::AVX512_SapphireRapids},
    {"tilezero_f32", Float(32, 256), "tile_zero", {Int(16), Int(16)}, Target::AVX512_SapphireRapids},
    {"tilestored64_i32", Int(32), "tile_store", {Int(16), Int(16), Handle(), Int(64), Int(64), Int(32, 256)}, Target::AVX512_SapphireRapids, x86Intrinsic::AccessesMemory},
    {"tilestored64_f32", Int(32), "tile_store", {Int(16), Int(16), Handle(), Int(64), Int(64), Float(32, 256)}, Target::AVX512_SapphireRapids, x86Intrinsic::AccessesMemory},
};
// clang-format on

void CodeGen_X86::init_module() {
    CodeGen_Posix::init_module();

    for (const x86Intrinsic &i : intrinsic_defs) {
        if (i.feature != Target::FeatureEnd && !target.has_feature(i.feature)) {
            continue;
        }

        Type ret_type = i.ret_type;
        vector<Type> arg_types;
        arg_types.reserve(max_intrinsic_args);
        for (halide_type_t j : i.arg_types) {
            if (j.bits == 0) {
                break;
            }
            arg_types.emplace_back(j);
        }

        auto *fn = declare_intrin_overload(i.name, ret_type, i.intrin_name, std::move(arg_types));
        if ((i.flags & x86Intrinsic::AccessesMemory) == 0) {
            function_does_not_access_memory(fn);
        }
        fn->addFnAttr(llvm::Attribute::NoUnwind);
    }
}

// i32(i16_a)*i32(i16_b) +/- i32(i16_c)*i32(i16_d) can be done by
// interleaving a, c, and b, d, and then using dot_product.
bool should_use_dot_product(const Expr &a, const Expr &b, vector<Expr> &result) {
    Type t = a.type();
    internal_assert(b.type() == t);

    if (!(t.is_int() && t.bits() == 32 && t.lanes() >= 4)) {
        return false;
    }

    const Call *ma = Call::as_intrinsic(a, {Call::widening_mul});
    const Call *mb = Call::as_intrinsic(b, {Call::widening_mul});
    // dot_product can't handle mixed type widening muls.
    if (ma && ma->args[0].type() != ma->args[1].type()) {
        return false;
    }
    if (mb && mb->args[0].type() != mb->args[1].type()) {
        return false;
    }
    // If the operands are widening shifts, we might be able to treat these as
    // multiplies.
    const Call *sa = Call::as_intrinsic(a, {Call::widening_shift_left});
    const Call *sb = Call::as_intrinsic(b, {Call::widening_shift_left});
    if (sa && !is_const(sa->args[1])) {
        sa = nullptr;
    }
    if (sb && !is_const(sb->args[1])) {
        sb = nullptr;
    }
    if ((ma || sa) && (mb || sb)) {
        Expr a0 = ma ? ma->args[0] : sa->args[0];
        Expr a1 = ma ? ma->args[1] : lossless_cast(sa->args[0].type(), simplify(make_const(sa->type, 1) << sa->args[1]));
        Expr b0 = mb ? mb->args[0] : sb->args[0];
        Expr b1 = mb ? mb->args[1] : lossless_cast(sb->args[0].type(), simplify(make_const(sb->type, 1) << sb->args[1]));
        if (a1.defined() && b1.defined()) {
            std::vector<Expr> args = {a0, a1, b0, b1};
            result.swap(args);
            return true;
        }
    }
    return false;
}

void CodeGen_X86::visit(const Add *op) {
    vector<Expr> matches;
    if (should_use_dot_product(op->a, op->b, matches)) {
        Expr ac = Shuffle::make_interleave({matches[0], matches[2]});
        Expr bd = Shuffle::make_interleave({matches[1], matches[3]});
        value = call_overloaded_intrin(op->type, "dot_product", {ac, bd});
        if (value) {
            return;
        }
    }
    CodeGen_Posix::visit(op);
}

void CodeGen_X86::visit(const Sub *op) {
    vector<Expr> matches;
    if (should_use_dot_product(op->a, op->b, matches)) {
        // Negate one of the factors in the second expression
        Expr negative_2 = lossless_negate(matches[2]);
        Expr negative_3 = lossless_negate(matches[3]);
        if (negative_2.defined() || negative_3.defined()) {
            if (negative_2.defined()) {
                matches[2] = negative_2;
            } else {
                matches[3] = negative_3;
            }
            Expr ac = Shuffle::make_interleave({matches[0], matches[2]});
            Expr bd = Shuffle::make_interleave({matches[1], matches[3]});
            value = call_overloaded_intrin(op->type, "dot_product", {ac, bd});
            if (value) {
                return;
            }
        }
    }
    CodeGen_Posix::visit(op);
}

void CodeGen_X86::visit(const GT *op) {
    Type t = op->a.type();

    if (t.is_vector() &&
        upgrade_type_for_arithmetic(t) == t) {
        // Non-native vector widths get legalized poorly by llvm. We
        // split it up ourselves.

        int slice_size = vector_lanes_for_slice(t);

        Value *a = codegen(op->a), *b = codegen(op->b);
        vector<Value *> result;
        for (int i = 0; i < op->type.lanes(); i += slice_size) {
            Value *sa = slice_vector(a, i, slice_size);
            Value *sb = slice_vector(b, i, slice_size);
            Value *slice_value;
            if (t.is_float()) {
                slice_value = builder->CreateFCmpOGT(sa, sb);
            } else if (t.is_int()) {
                slice_value = builder->CreateICmpSGT(sa, sb);
            } else {
                slice_value = builder->CreateICmpUGT(sa, sb);
            }
            result.push_back(slice_value);
        }

        value = concat_vectors(result);
        value = slice_vector(value, 0, t.lanes());
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_X86::visit(const EQ *op) {
    Type t = op->a.type();

    if (t.is_vector() &&
        upgrade_type_for_arithmetic(t) == t) {
        // Non-native vector widths get legalized poorly by llvm. We
        // split it up ourselves.

        int slice_size = vector_lanes_for_slice(t);

        Value *a = codegen(op->a), *b = codegen(op->b);
        vector<Value *> result;
        for (int i = 0; i < op->type.lanes(); i += slice_size) {
            Value *sa = slice_vector(a, i, slice_size);
            Value *sb = slice_vector(b, i, slice_size);
            Value *slice_value;
            if (t.is_float()) {
                slice_value = builder->CreateFCmpOEQ(sa, sb);
            } else {
                slice_value = builder->CreateICmpEQ(sa, sb);
            }
            result.push_back(slice_value);
        }

        value = concat_vectors(result);
        value = slice_vector(value, 0, t.lanes());
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_X86::visit(const LT *op) {
    codegen(op->b > op->a);
}

void CodeGen_X86::visit(const LE *op) {
    codegen(!(op->a > op->b));
}

void CodeGen_X86::visit(const GE *op) {
    codegen(!(op->b > op->a));
}

void CodeGen_X86::visit(const NE *op) {
    codegen(!(op->a == op->b));
}

void CodeGen_X86::visit(const Select *op) {
    if (op->condition.type().is_vector()) {
        // LLVM handles selects on vector conditions much better at native width
        Value *cond = codegen(op->condition);
        Value *true_val = codegen(op->true_value);
        Value *false_val = codegen(op->false_value);
        Type t = op->true_value.type();
        int slice_size = vector_lanes_for_slice(t);

        vector<Value *> result;
        for (int i = 0; i < t.lanes(); i += slice_size) {
            Value *st = slice_vector(true_val, i, slice_size);
            Value *sf = slice_vector(false_val, i, slice_size);
            Value *sc = slice_vector(cond, i, slice_size);
            Value *slice_value = builder->CreateSelect(sc, st, sf);
            result.push_back(slice_value);
        }

        value = concat_vectors(result);
        value = slice_vector(value, 0, t.lanes());
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_X86::visit(const Cast *op) {
    Type src = op->value.type();
    Type dst = op->type;

    if (target.has_feature(Target::F16C) &&
        dst.code() == Type::Float &&
        src.code() == Type::Float &&
        (dst.bits() == 16 || src.bits() == 16)) {
        // Node we use code() == Type::Float instead of is_float(), because we
        // don't want to catch bfloat casts.

        // This target doesn't support full float16 arithmetic, but it *does*
        // support float16 casts, so we emit a vanilla LLVM cast node.
        value = codegen(op->value);
        value = builder->CreateFPCast(value, llvm_type_of(dst));
        return;
    }

    if (!dst.is_vector()) {
        // We only have peephole optimizations for vectors after this point.
        CodeGen_Posix::visit(op);
        return;
    }

    struct Pattern {
        string intrin;
        Expr pattern;
    };

    // clang-format off
    static Pattern patterns[] = {
        // This isn't rounding_multiply_quantzied(i16, i16, 15) because it doesn't
        // saturate the result.
        {"pmulhrs", i16(rounding_shift_right(widening_mul(wild_i16x_, wild_i16x_), 15))},

        {"f32_to_bf16", bf16(wild_f32x_)},
    };
    // clang-format on

    vector<Expr> matches;
    for (const Pattern &p : patterns) {
        if (expr_match(p.pattern, op, matches)) {
            value = call_overloaded_intrin(dst, p.intrin, matches);
            if (value) {
                return;
            }
        }
    }

    if (const Call *mul = Call::as_intrinsic(op->value, {Call::widening_mul})) {
        if (src.bits() < dst.bits() && dst.bits() <= 32) {
            // LLVM/x86 really doesn't like 8 -> 16 bit multiplication. If we're
            // widening to 32-bits after a widening multiply, LLVM prefers to see a
            // widening multiply directly to 32-bits. This may result in extra
            // casts, so simplify to remove them.
            value = codegen(simplify(Mul::make(Cast::make(dst, mul->args[0]), Cast::make(dst, mul->args[1]))));
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_X86::visit(const Call *op) {
    if (op->is_intrinsic(Call::round)) {
        value = call_overloaded_intrin(op->type, "round", op->args);
        if (value) {
            return;
        }
    }

    if (!op->type.is_vector()) {
        // We only have peephole optimizations for vectors beyond this point.
        CodeGen_Posix::visit(op);
        return;
    }

    // A 16-bit mul-shift-right of less than 16 can sometimes be rounded up to a
    // full 16 to use pmulh(u)w by left-shifting one of the operands. This is
    // handled here instead of in the lowering of mul_shift_right because it's
    // unlikely to be a good idea on platforms other than x86, as it adds an
    // extra shift in the fully-lowered case.
    if ((op->type.element_of() == UInt(16) ||
         op->type.element_of() == Int(16)) &&
        op->is_intrinsic(Call::mul_shift_right)) {
        internal_assert(op->args.size() == 3);
        const uint64_t *shift = as_const_uint(op->args[2]);
        if (shift && *shift < 16 && *shift >= 8) {
            Type narrow = op->type.with_bits(8);
            Expr narrow_a = lossless_cast(narrow, op->args[0]);
            Expr narrow_b = narrow_a.defined() ? Expr() : lossless_cast(narrow, op->args[1]);
            int shift_left = 16 - (int)(*shift);
            if (narrow_a.defined()) {
                codegen(mul_shift_right(op->args[0] << shift_left, op->args[1], 16));
                return;
            } else if (narrow_b.defined()) {
                codegen(mul_shift_right(op->args[0], op->args[1] << shift_left, 16));
                return;
            }
        }
    } else if (op->type.is_int() &&
               op->type.bits() <= 16 &&
               op->is_intrinsic(Call::rounding_halving_add)) {
        // We can redirect signed rounding halving add to unsigned rounding
        // halving add by adding 128 / 32768 to the result if the sign of the
        // args differs.
        internal_assert(op->args.size() == 2);
        Type t = op->type.with_code(halide_type_uint);
        Expr a = cast(t, op->args[0]);
        Expr b = cast(t, op->args[1]);
        codegen(cast(op->type, rounding_halving_add(a, b) + ((a ^ b) & (1 << (t.bits() - 1)))));
        return;
    } else if (op->is_intrinsic(Call::absd)) {
        internal_assert(op->args.size() == 2);
        if (op->args[0].type().is_uint()) {
            // On x86, there are many 3-instruction sequences to compute absd of
            // unsigned integers. This one consists solely of instructions with
            // throughput of 3 ops per cycle on Cannon Lake.
            //
            // Solution due to Wojciech Mula:
            // http://0x80.pl/notesen/2018-03-11-sse-abs-unsigned.html
            codegen(saturating_sub(op->args[0], op->args[1]) | saturating_sub(op->args[1], op->args[0]));
            return;
        } else if (op->args[0].type().is_int()) {
            // In the signed case, we take the min/max, cast them to unsigned,
            // and subtract. The cast to unsigned may wrap, but if it does, so
            // will the subtract.
            codegen(
                cast(op->type, Max::make(op->args[0], op->args[1])) -
                cast(op->type, Min::make(op->args[0], op->args[1])));
            return;
        }
    }

    struct Pattern {
        string intrin;
        Expr pattern;
    };

    // clang-format off
    static Pattern patterns[] = {
        {"pmulh", mul_shift_right(wild_i16x_, wild_i16x_, 16)},
        {"pmulh", mul_shift_right(wild_u16x_, wild_u16x_, 16)},
        {"saturating_narrow", i16_sat(wild_i32x_)},
        {"saturating_narrow", u16_sat(wild_i32x_)},
        {"saturating_narrow", i8_sat(wild_i16x_)},
        {"saturating_narrow", u8_sat(wild_i16x_)},
    };
    // clang-format on

    vector<Expr> matches;
    for (const auto &pattern : patterns) {
        if (expr_match(pattern.pattern, op, matches)) {
            value = call_overloaded_intrin(op->type, pattern.intrin, matches);
            if (value) {
                return;
            }
        }
    }

    static const vector<pair<Expr, Expr>> cast_rewrites = {
        // Some double-narrowing saturating casts can be better expressed as
        // combinations of single-narrowing saturating casts.
        {u8_sat(wild_i32x_), u8_sat(i16_sat(wild_i32x_))},
        {i8_sat(wild_i32x_), i8_sat(i16_sat(wild_i32x_))},
    };
    for (const auto &i : cast_rewrites) {
        if (expr_match(i.first, op, matches)) {
            Expr replacement = substitute("*", matches[0], with_lanes(i.second, op->type.lanes()));
            value = codegen(replacement);
            return;
        }
    }

    // Check for saturating_pmulhrs. On x86, pmulhrs is truncating, but it's still faster
    // to use pmulhrs than to lower (producing widening multiplication), and have a check
    // for the singular overflow case.
    static Expr saturating_pmulhrs = rounding_mul_shift_right(wild_i16x_, wild_i16x_, 15);
    if (expr_match(saturating_pmulhrs, op, matches)) {
        // Rewrite so that we can take advantage of pmulhrs.
        internal_assert(matches.size() == 2);
        internal_assert(op->type.element_of() == Int(16));
        const Expr &a = matches[0];
        const Expr &b = matches[1];

        Expr pmulhrs = i16(rounding_shift_right(widening_mul(a, b), 15));

        Expr i16_min = op->type.min();
        Expr i16_max = op->type.max();

        // Handle edge case of possible overflow.
        // See https://github.com/halide/Halide/pull/7129/files#r1008331426
        // On AVX512 (and with enough lanes) we can use a mask register.
        if (target.has_feature(Target::AVX512) && op->type.lanes() >= 32) {
            Expr expr = select((a == i16_min) && (b == i16_min), i16_max, pmulhrs);
            expr.accept(this);
        } else {
            Expr mask = select(max(a, b) == i16_min, cast(op->type, -1), cast(op->type, 0));
            Expr expr = mask ^ pmulhrs;
            expr.accept(this);
        }

        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_X86::codegen_vector_reduce(const VectorReduce *op, const Expr &init) {
    if (op->op != VectorReduce::Add && op->op != VectorReduce::SaturatingAdd) {
        CodeGen_Posix::codegen_vector_reduce(op, init);
        return;
    }
    const int factor = op->value.type().lanes() / op->type.lanes();

    struct Pattern {
        VectorReduce::Operator reduce_op;
        int factor;
        Expr pattern;
        const char *intrin;
        Type narrow_type;
        uint32_t flags = 0;
        enum {
            CombineInit = 1 << 0,
            SwapOperands = 1 << 1,
            SingleArg = 1 << 2,
        };
    };
    // clang-format off
    // These patterns are roughly sorted "best to worst", in case there are two
    // patterns that match the expression.
    static const Pattern patterns[] = {
        // 4-way dot products
        {VectorReduce::Add, 4, i32(widening_mul(wild_u8x_, wild_i8x_)), "dot_product", {}, Pattern::CombineInit},
        {VectorReduce::Add, 4, i32(widening_mul(wild_i8x_, wild_u8x_)), "dot_product", {}, Pattern::CombineInit | Pattern::SwapOperands},
        {VectorReduce::SaturatingAdd, 4, i32(widening_mul(wild_u8x_, wild_i8x_)), "saturating_dot_product", {}, Pattern::CombineInit},
        {VectorReduce::SaturatingAdd, 4, i32(widening_mul(wild_i8x_, wild_u8x_)), "saturating_dot_product", {}, Pattern::CombineInit | Pattern::SwapOperands},

        // 2-way dot products
        {VectorReduce::Add, 2, i32(widening_mul(wild_i8x_, wild_i8x_)), "dot_product", Int(16)},
        {VectorReduce::Add, 2, i32(widening_mul(wild_i8x_, wild_u8x_)), "dot_product", Int(16)},
        {VectorReduce::Add, 2, i32(widening_mul(wild_u8x_, wild_i8x_)), "dot_product", Int(16)},
        {VectorReduce::Add, 2, i32(widening_mul(wild_u8x_, wild_u8x_)), "dot_product", Int(16)},
        {VectorReduce::SaturatingAdd, 2, i32(widening_mul(wild_u8x_, wild_i8x_)), "saturating_dot_product", {}, Pattern::CombineInit},
        {VectorReduce::SaturatingAdd, 2, i32(widening_mul(wild_i8x_, wild_u8x_)), "saturating_dot_product", {}, Pattern::CombineInit | Pattern::SwapOperands},
        {VectorReduce::SaturatingAdd, 2, widening_mul(wild_u8x_, wild_i8x_), "saturating_dot_product"},
        {VectorReduce::SaturatingAdd, 2, widening_mul(wild_i8x_, wild_u8x_), "saturating_dot_product", {}, Pattern::SwapOperands},

        {VectorReduce::Add, 2, i32(widening_mul(wild_i16x_, wild_i16x_)), "dot_product", {}, Pattern::CombineInit},
        {VectorReduce::Add, 2, i32(widening_mul(wild_i16x_, wild_i16x_)), "dot_product", Int(16)},
        {VectorReduce::SaturatingAdd, 2, i32(widening_mul(wild_i16x_, wild_i16x_)), "saturating_dot_product", {}, Pattern::CombineInit},

        {VectorReduce::Add, 2, wild_f32x_ * wild_f32x_, "dot_product", BFloat(16), Pattern::CombineInit},

        // Horizontal widening addition using a dot_product against a vector of ones.
        {VectorReduce::Add, 2, u16(wild_u8x_), "horizontal_widening_add", {}, Pattern::SingleArg},
        {VectorReduce::Add, 2, i16(wild_u8x_), "horizontal_widening_add", {}, Pattern::SingleArg},
        {VectorReduce::Add, 2, i16(wild_i8x_), "horizontal_widening_add", {}, Pattern::SingleArg},
        {VectorReduce::Add, 2, i32(wild_i16x_), "horizontal_widening_add", {}, Pattern::SingleArg},

        // Sum of absolute differences
        {VectorReduce::Add, 8, u64(absd(wild_u8x_, wild_u8x_)), "sum_of_absolute_differences", {}},

    };
    // clang-format on

    std::vector<Expr> matches;
    for (const Pattern &p : patterns) {
        if (op->op != p.reduce_op || p.factor != factor) {
            continue;
        }
        if (expr_match(p.pattern, op->value, matches)) {
            if (p.flags & Pattern::SingleArg) {
                Expr a = matches[0];

                if (p.narrow_type.bits() > 0) {
                    a = lossless_cast(p.narrow_type.with_lanes(a.type().lanes()), a);
                }
                if (!a.defined()) {
                    continue;
                }

                if (init.defined() && (p.flags & Pattern::CombineInit)) {
                    value = call_overloaded_intrin(op->type, p.intrin, {init, a});
                    if (value) {
                        return;
                    }
                } else {
                    value = call_overloaded_intrin(op->type, p.intrin, {a});
                    if (value) {
                        if (init.defined()) {
                            Value *x = value;
                            Value *y = codegen(init);
                            value = builder->CreateAdd(x, y);
                        }
                        return;
                    }
                }
            } else {
                Expr a = matches[0];
                Expr b = matches[1];
                if (p.flags & Pattern::SwapOperands) {
                    std::swap(a, b);
                }
                if (p.narrow_type.bits() > 0) {
                    a = lossless_cast(p.narrow_type.with_lanes(a.type().lanes()), a);
                    b = lossless_cast(p.narrow_type.with_lanes(b.type().lanes()), b);
                }
                if (!a.defined() || !b.defined()) {
                    continue;
                }

                if (init.defined() && (p.flags & Pattern::CombineInit)) {
                    value = call_overloaded_intrin(op->type, p.intrin, {init, a, b});
                    if (value) {
                        return;
                    }
                } else {
                    value = call_overloaded_intrin(op->type, p.intrin, {a, b});
                    if (value) {
                        if (init.defined()) {
                            Value *x = value;
                            Value *y = codegen(init);
                            value = builder->CreateAdd(x, y);
                        }
                        return;
                    }
                }
            }
        }
    }

    // Rewrite non-native sum-of-absolute-difference variants to the native
    // op. We support reducing to various types. We could consider supporting
    // multiple reduction factors too, but in general we don't handle non-native
    // reduction factors for VectorReduce nodes (yet?).
    if (op->op == VectorReduce::Add &&
        factor == 8) {
        const Cast *cast = op->value.as<Cast>();
        const Call *call = cast ? cast->value.as<Call>() : nullptr;
        if (call &&
            call->is_intrinsic(Call::absd) &&
            cast->type.element_of().can_represent(UInt(8)) &&
            (cast->type.is_int() || cast->type.is_uint()) &&
            call->args[0].type().element_of() == UInt(8)) {

            internal_assert(cast->type.element_of() != UInt(64)) << "Should have pattern-matched above\n";

            // Cast to uint64 instead
            Expr equiv = Cast::make(UInt(64, cast->value.type().lanes()), cast->value);
            // Reduce on that to hit psadbw
            equiv = VectorReduce::make(VectorReduce::Add, equiv, op->type.lanes());
            // Then cast that to the desired type
            equiv = Cast::make(cast->type.with_lanes(equiv.type().lanes()), equiv);
            codegen(equiv);
            return;
        }
    }

    CodeGen_Posix::codegen_vector_reduce(op, init);
}

void CodeGen_X86::visit(const Allocate *op) {
    ScopedBinding<MemoryType> bind(mem_type, op->name, op->memory_type);
    CodeGen_Posix::visit(op);
}

void CodeGen_X86::visit(const Load *op) {
    if (mem_type.contains(op->name) && mem_type.get(op->name) == MemoryType::AMXTile) {
        const Ramp *ramp = op->index.as<Ramp>();
        internal_assert(ramp) << "Expected AMXTile to have index ramp\n";
        Value *ptr = codegen_buffer_pointer(op->name, op->type, ramp->base);
        LoadInst *load = builder->CreateAlignedLoad(llvm_type_of(upgrade_type_for_storage(op->type)), ptr, llvm::Align(op->type.bytes()));
        add_tbaa_metadata(load, op->name, op->index);
        value = load;
        return;
    }
    CodeGen_Posix::visit(op);
}

void CodeGen_X86::visit(const Store *op) {
    if (mem_type.contains(op->name) && mem_type.get(op->name) == MemoryType::AMXTile) {
        Value *val = codegen(op->value);
        Halide::Type value_type = op->value.type();
        const Ramp *ramp = op->index.as<Ramp>();
        internal_assert(ramp) << "Expected AMXTile to have index ramp\n";
        Value *ptr = codegen_buffer_pointer(op->name, value_type, ramp->base);
        StoreInst *store = builder->CreateAlignedStore(val, ptr, llvm::Align(value_type.bytes()));
        add_tbaa_metadata(store, op->name, op->index);
        return;
    }
    CodeGen_Posix::visit(op);
}

string CodeGen_X86::mcpu_target() const {
    // Perform an ad-hoc guess for the -mcpu given features.
    // WARNING: this is used to drive -mcpu, *NOT* -mtune!
    //          The CPU choice here *WILL* affect -mattrs!
    if (target.has_feature(Target::AVX512_SapphireRapids)) {
        return "sapphirerapids";
    } else if (target.has_feature(Target::AVX512_Zen4)) {
        return "znver4";
    } else if (target.has_feature(Target::AVX512_Cannonlake)) {
        return "cannonlake";
    } else if (target.has_feature(Target::AVX512_Skylake)) {
        return "skylake-avx512";
    } else if (target.has_feature(Target::AVX512_KNL)) {
        return "knl";
    } else if (target.has_feature(Target::AVX2)) {
        return "haswell";
    } else if (target.has_feature(Target::AVX)) {
        return "corei7-avx";
    } else if (target.has_feature(Target::SSE41)) {
        // We want SSE4.1 but not SSE4.2, hence "penryn" rather than "corei7"
        return "penryn";
    } else {
        // Default should not include SSSE3, hence "k8" rather than "core2"
        return "k8";
    }
}

namespace {
bool gather_might_be_slow(Target target) {
    // Intel x86 processors between broadwell and tiger lake have a microcode
    // mitigation that makes gather instructions very slow. If we know we're on
    // an AMD processor, gather is safe to use. If we have the AVX512 extensions
    // present in Zen4 (or above), we also know we're not on an affected
    // processor.
    switch (target.processor_tune) {
    case Target::Processor::AMDFam10:
    case Target::Processor::BdVer1:
    case Target::Processor::BdVer2:
    case Target::Processor::BdVer3:
    case Target::Processor::BdVer4:
    case Target::Processor::BtVer1:
    case Target::Processor::BtVer2:
    case Target::Processor::K8:
    case Target::Processor::K8_SSE3:
    case Target::Processor::ZnVer1:
    case Target::Processor::ZnVer2:
    case Target::Processor::ZnVer3:
    case Target::Processor::ZnVer4:
        return false;
    default:
        return !target.has_feature(Target::AVX512_Zen4);
    }
}
}  // namespace

string CodeGen_X86::mcpu_tune() const {
    // Check if any explicit request for tuning exists.
    switch (target.processor_tune) {  // Please keep sorted.
    case Target::Processor::AMDFam10:
        return "amdfam10";
    case Target::Processor::BdVer1:
        return "bdver1";
    case Target::Processor::BdVer2:
        return "bdver2";
    case Target::Processor::BdVer3:
        return "bdver3";
    case Target::Processor::BdVer4:
        return "bdver4";
    case Target::Processor::BtVer1:
        return "btver1";
    case Target::Processor::BtVer2:
        return "btver2";
    case Target::Processor::K8:
        return "k8";
    case Target::Processor::K8_SSE3:
        return "k8-sse3";
    case Target::Processor::ZnVer1:
        return "znver1";
    case Target::Processor::ZnVer2:
        return "znver2";
    case Target::Processor::ZnVer3:
        return "znver3";
    case Target::Processor::ZnVer4:
        return "znver4";

    case Target::Processor::ProcessorGeneric:
        break;
    }
    internal_assert(target.processor_tune == Target::Processor::ProcessorGeneric && "The switch should be exhaustive.");
    return mcpu_target();  // Detect "best" CPU from the enabled ISA's.
}

// FIXME: we should lower everything here, instead of relying
//        that -mcpu= (`mcpu_target()`) implies/sets features for us.
string CodeGen_X86::mattrs() const {
    std::vector<std::string_view> attrs;
    if (target.has_feature(Target::FMA)) {
        attrs.emplace_back("+fma");
    }
    if (target.has_feature(Target::FMA4)) {
        attrs.emplace_back("+fma4");
    }
    if (target.has_feature(Target::F16C)) {
        attrs.emplace_back("+f16c");
    }
    if (target.has_feature(Target::AVX512) ||
        target.has_feature(Target::AVX512_KNL) ||
        target.has_feature(Target::AVX512_Skylake) ||
        target.has_feature(Target::AVX512_Cannonlake)) {
        attrs.emplace_back("+avx512f");
        attrs.emplace_back("+avx512cd");
        if (target.has_feature(Target::AVX512_KNL)) {
            attrs.emplace_back("+avx512pf");
            attrs.emplace_back("+avx512er");
        }
        if (target.has_feature(Target::AVX512_Skylake) ||
            target.has_feature(Target::AVX512_Cannonlake)) {
            attrs.emplace_back("+avx512vl");
            attrs.emplace_back("+avx512bw");
            attrs.emplace_back("+avx512dq");
        }
        if (target.has_feature(Target::AVX512_Cannonlake)) {
            attrs.emplace_back("+avx512ifma");
            attrs.emplace_back("+avx512vbmi");
        }
        if (target.has_feature(Target::AVX512_Zen4)) {
            attrs.emplace_back("+avx512bf16");
            attrs.emplace_back("+avx512vnni");
            attrs.emplace_back("+avx512bitalg");
            attrs.emplace_back("+avx512vbmi2");
        }
        if (target.has_feature(Target::AVX512_SapphireRapids)) {
            attrs.emplace_back("+avxvnni");
            attrs.emplace_back("+amx-int8");
            attrs.emplace_back("+amx-bf16");
        }
    }
#if LLVM_VERSION >= 180
    if (gather_might_be_slow(target)) {
        attrs.push_back("+prefer-no-gather");
    }
#endif
    return join_strings(attrs, ",");
}

bool CodeGen_X86::use_soft_float_abi() const {
    return false;
}

int CodeGen_X86::native_vector_bits() const {
    if (target.has_feature(Target::AVX512) ||
        target.has_feature(Target::AVX512_Skylake) ||
        target.has_feature(Target::AVX512_KNL) ||
        target.has_feature(Target::AVX512_Cannonlake)) {
        return 512;
    } else if (target.has_feature(Target::AVX) ||
               target.has_feature(Target::AVX2)) {
        return 256;
    } else {
        return 128;
    }
}

int CodeGen_X86::vector_lanes_for_slice(const Type &t) const {
    // We don't want to pad all the way out to natural_vector_size,
    // because llvm generates crappy code. Better to use a smaller
    // type if we can.
    int vec_bits = t.lanes() * t.bits();
    int natural_vec_bits = target.natural_vector_size(t) * t.bits();
    // clang-format off
    int slice_bits = ((vec_bits > 256 && natural_vec_bits > 256) ? 512 :
                      (vec_bits > 128 && natural_vec_bits > 128) ? 256 :
                                                                   128);
    // clang-format on
    return slice_bits / t.bits();
}

}  // namespace

std::unique_ptr<CodeGen_Posix> new_CodeGen_X86(const Target &target) {
    return std::make_unique<CodeGen_X86>(target);
}

#else  // WITH_X86

std::unique_ptr<CodeGen_Posix> new_CodeGen_X86(const Target &target) {
    user_error << "x86 not enabled for this build of Halide.\n";
    return nullptr;
}

#endif  // WITH_X86

}  // namespace Internal
}  // namespace Halide
