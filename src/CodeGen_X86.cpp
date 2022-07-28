#include "CodeGen_Posix.h"
#include "ConciseCasts.h"
#include "Debug.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "LLVM_Headers.h"
#include "Simplify.h"
#include "Util.h"
#include "X86Optimize.h"

namespace Halide {
namespace Internal {

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
        t.set_feature(Target::AVX512_Cannonlake);
    }
    if (t.has_feature(Target::AVX512_Cannonlake)) {
        t.set_feature(Target::AVX512_Skylake);
    }
    if (t.has_feature(Target::AVX512_Cannonlake) ||
        t.has_feature(Target::AVX512_Skylake) ||
        t.has_feature(Target::AVX512_KNL)) {
        t.set_feature(Target::AVX2);
    }
    if (t.has_feature(Target::AVX2)) {
        t.set_feature(Target::AVX);
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
    void compile_func(const LoweredFunc &f,
                      const std::string &simple_name, const std::string &extern_name) override;

    string mcpu_target() const override;
    string mcpu_tune() const override;
    string mattrs() const override;
    bool use_soft_float_abi() const override;
    int native_vector_bits() const override;

    int vector_lanes_for_slice(const Type &t) const;

    llvm::Type *llvm_type_of(const Type &t) const override;

    using CodeGen_Posix::visit;

    void init_module() override;

    /** Nodes for which we want to emit specific sse/avx intrinsics */
    // @{
    void visit(const Cast *) override;
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
    void visit(const VectorInstruction *) override;
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
    {"abs_i8x32", UInt(8, 32), "abs", {Int(8, 32)}, Target::AVX2},
    {"abs_i16x16", UInt(16, 16), "abs", {Int(16, 16)}, Target::AVX2},
    {"abs_i32x8", UInt(32, 8), "abs", {Int(32, 8)}, Target::AVX2},
    {"abs_f32x8", Float(32, 8), "abs", {Float(32, 8)}, Target::AVX2},
    {"abs_i8x16", UInt(8, 16), "abs", {Int(8, 16)}, Target::SSE41},
    {"abs_i16x8", UInt(16, 8), "abs", {Int(16, 8)}, Target::SSE41},
    {"abs_i32x4", UInt(32, 4), "abs", {Int(32, 4)}, Target::SSE41},
    {"abs_f32x4", Float(32, 4), "abs", {Float(32, 4)}},

    {"llvm.sadd.sat.v32i8", Int(8, 32), "saturating_add", {Int(8, 32), Int(8, 32)}, Target::AVX2},
    {"llvm.sadd.sat.v16i8", Int(8, 16), "saturating_add", {Int(8, 16), Int(8, 16)}},
    {"llvm.sadd.sat.v8i8", Int(8, 8), "saturating_add", {Int(8, 8), Int(8, 8)}},
    {"llvm.ssub.sat.v32i8", Int(8, 32), "saturating_sub", {Int(8, 32), Int(8, 32)}, Target::AVX2},
    {"llvm.ssub.sat.v16i8", Int(8, 16), "saturating_sub", {Int(8, 16), Int(8, 16)}},
    {"llvm.ssub.sat.v8i8", Int(8, 8), "saturating_sub", {Int(8, 8), Int(8, 8)}},

    {"llvm.sadd.sat.v16i16", Int(16, 16), "saturating_add", {Int(16, 16), Int(16, 16)}, Target::AVX2},
    {"llvm.sadd.sat.v8i16", Int(16, 8), "saturating_add", {Int(16, 8), Int(16, 8)}},
    {"llvm.ssub.sat.v16i16", Int(16, 16), "saturating_sub", {Int(16, 16), Int(16, 16)}, Target::AVX2},
    {"llvm.ssub.sat.v8i16", Int(16, 8), "saturating_sub", {Int(16, 8), Int(16, 8)}},

    // Sum of absolute differences
    {"llvm.x86.sse2.psad.bw", UInt(64, 2), "sum_absd", {UInt(8, 16), UInt(8, 16)}},
    {"llvm.x86.avx2.psad.bw", UInt(64, 4), "sum_absd", {UInt(8, 32), UInt(8, 32)}, Target::AVX2},
    {"llvm.x86.avx512.psad.bw.512", UInt(64, 8), "sum_absd", {UInt(8, 64), UInt(8, 64)}, Target::AVX512_Skylake},

    // Some of the instructions referred to below only appear with
    // AVX2, but LLVM generates better AVX code if you give it
    // full 256-bit vectors and let it do the slicing up into
    // individual instructions itself. This is why we use
    // Target::AVX instead of Target::AVX2 as the feature flag
    // requirement.
    // TODO: Just use llvm.*add/*sub.sat, and verify the above comment?
    {"paddusbx32", UInt(8, 32), "saturating_add", {UInt(8, 32), UInt(8, 32)}, Target::AVX},
    {"paddusbx16", UInt(8, 16), "saturating_add", {UInt(8, 16), UInt(8, 16)}},
    {"psubusbx32", UInt(8, 32), "saturating_sub", {UInt(8, 32), UInt(8, 32)}, Target::AVX},
    {"psubusbx16", UInt(8, 16), "saturating_sub", {UInt(8, 16), UInt(8, 16)}},

    {"padduswx16", UInt(16, 16), "saturating_add", {UInt(16, 16), UInt(16, 16)}, Target::AVX},
    {"padduswx8", UInt(16, 8), "saturating_add", {UInt(16, 8), UInt(16, 8)}},
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
    {"wmul_pmaddwd_avx2", Int(32, 8), "widening_mul", {Int(16, 8), Int(16, 8)}, Target::AVX2},
    {"wmul_pmaddwd_sse2", Int(32, 4), "widening_mul", {Int(16, 4), Int(16, 4)}},

    // Multiply keep high half
    {"llvm.x86.avx2.pmulh.w", Int(16, 16), "pmulh", {Int(16, 16), Int(16, 16)}, Target::AVX2},
    {"llvm.x86.avx2.pmulhu.w", UInt(16, 16), "pmulh", {UInt(16, 16), UInt(16, 16)}, Target::AVX2},
    {"llvm.x86.avx2.pmul.hr.sw", Int(16, 16), "pmulhrs", {Int(16, 16), Int(16, 16)}, Target::AVX2},
    {"llvm.x86.sse2.pmulh.w", Int(16, 8), "pmulh", {Int(16, 8), Int(16, 8)}},
    {"llvm.x86.sse2.pmulhu.w", UInt(16, 8), "pmulh", {UInt(16, 8), UInt(16, 8)}},
    {"llvm.x86.ssse3.pmul.hr.sw.128", Int(16, 8), "pmulhrs", {Int(16, 8), Int(16, 8)}, Target::SSE41},

    // Convert FP32 to BF16
    {"vcvtne2ps2bf16x32", BFloat(16, 32), "f32_to_bf16", {Float(32, 32)}, Target::AVX512_SapphireRapids},
    {"llvm.x86.avx512bf16.cvtneps2bf16.512", BFloat(16, 16), "f32_to_bf16", {Float(32, 16)}, Target::AVX512_SapphireRapids},
    {"llvm.x86.avx512bf16.cvtneps2bf16.256", BFloat(16, 8), "f32_to_bf16", {Float(32, 8)}, Target::AVX512_SapphireRapids},
    // LLVM does not provide an unmasked 128bit cvtneps2bf16 intrinsic, so provide a wrapper around the masked version.
    {"vcvtneps2bf16x4", BFloat(16, 4), "f32_to_bf16", {Float(32, 4)}, Target::AVX512_SapphireRapids},

    // Horizontal adds that use (v)phadd(w | d).
    {"phaddw_sse3", UInt(16, 8), "horizontal_add", {UInt(16, 16)}, Target::SSE41},
    {"phaddw_sse3", Int(16, 8), "horizontal_add", {Int(16, 16)}, Target::SSE41},
    {"phaddw_avx2", UInt(16, 16), "horizontal_add", {UInt(16, 32)}, Target::AVX2},
    {"phaddw_avx2", Int(16, 16), "horizontal_add", {Int(16, 32)}, Target::AVX2},
    {"phaddd_sse3", UInt(32, 4), "horizontal_add", {UInt(32, 8)}, Target::SSE41},
    {"phaddd_sse3", Int(32, 4), "horizontal_add", {Int(32, 8)}, Target::SSE41},
    {"phaddd_avx2", UInt(32, 8), "horizontal_add", {UInt(32, 16)}, Target::AVX2},
    {"phaddd_avx2", Int(32, 8), "horizontal_add", {Int(32, 16)}, Target::AVX2},

    // 2-way dot products
    {"llvm.x86.avx2.pmadd.ub.sw", Int(16, 16), "saturating_dot_product", {UInt(8, 32), Int(8, 32)}, Target::AVX2},
    {"llvm.x86.ssse3.pmadd.ub.sw.128", Int(16, 8), "saturating_dot_product", {UInt(8, 16), Int(8, 16)}, Target::SSE41},

    // Horizontal widening adds using 2-way dot products.
    {"hadd_pmadd_u8_sse3", UInt(16, 8), "horizontal_widening_add", {UInt(8, 16)}, Target::SSE41},
    {"hadd_pmadd_u8_sse3", Int(16, 8), "horizontal_widening_add", {UInt(8, 16)}, Target::SSE41},
    {"hadd_pmadd_i8_sse3", Int(16, 8), "horizontal_widening_add", {Int(8, 16)}, Target::SSE41},
    {"hadd_pmadd_u8_avx2", UInt(16, 16), "horizontal_widening_add", {UInt(8, 32)}, Target::AVX2},
    {"hadd_pmadd_u8_avx2", Int(16, 16), "horizontal_widening_add", {UInt(8, 32)}, Target::AVX2},
    {"hadd_pmadd_i8_avx2", Int(16, 16), "horizontal_widening_add", {Int(8, 32)}, Target::AVX2},

    {"llvm.x86.avx512.pmaddw.d.512", Int(32, 16), "dot_product", {Int(16, 32), Int(16, 32)}, Target::AVX512_Skylake},
    {"llvm.x86.avx512.pmaddw.d.512", Int(32, 16), "dot_product", {Int(16, 32), Int(16, 32)}, Target::AVX512_Cannonlake},
    {"llvm.x86.avx2.pmadd.wd", Int(32, 8), "dot_product", {Int(16, 16), Int(16, 16)}, Target::AVX2},
    {"llvm.x86.sse2.pmadd.wd", Int(32, 4), "dot_product", {Int(16, 8), Int(16, 8)}},

    // 4-way dot product vector reduction
    // The LLVM intrinsics combine the bf16 pairs into i32, so provide a wrapper to correctly call the intrinsic.
    {"dpbf16psx16", Float(32, 16), "dot_product", {Float(32, 16), BFloat(16, 32), BFloat(16, 32)}, Target::AVX512_SapphireRapids},
    {"dpbf16psx8", Float(32, 8), "dot_product", {Float(32, 8), BFloat(16, 16), BFloat(16, 16)}, Target::AVX512_SapphireRapids},
    {"dpbf16psx4", Float(32, 4), "dot_product", {Float(32, 4), BFloat(16, 8), BFloat(16, 8)}, Target::AVX512_SapphireRapids},

    {"dpbusdx16", Int(32, 16), "dot_product", {Int(32, 16), UInt(8, 64), Int(8, 64)}, Target::AVX512_SapphireRapids},
    {"dpbusdx8", Int(32, 8), "dot_product", {Int(32, 8), UInt(8, 32), Int(8, 32)}, Target::AVX512_SapphireRapids},
    {"dpbusdx4", Int(32, 4), "dot_product", {Int(32, 4), UInt(8, 16), Int(8, 16)}, Target::AVX512_SapphireRapids},

    {"dpwssdx16", Int(32, 16), "dot_product", {Int(32, 16), Int(16, 32), Int(16, 32)}, Target::AVX512_SapphireRapids},
    {"dpwssdx8", Int(32, 8), "dot_product", {Int(32, 8), Int(16, 16), Int(16, 16)}, Target::AVX512_SapphireRapids},
    {"dpwssdx4", Int(32, 4), "dot_product", {Int(32, 4), Int(16, 8), Int(16, 8)}, Target::AVX512_SapphireRapids},

    {"dpbusdsx16", Int(32, 16), "saturating_dot_product", {Int(32, 16), UInt(8, 64), Int(8, 64)}, Target::AVX512_SapphireRapids},
    {"dpbusdsx8", Int(32, 8), "saturating_dot_product", {Int(32, 8), UInt(8, 32), Int(8, 32)}, Target::AVX512_SapphireRapids},
    {"dpbusdsx4", Int(32, 4), "saturating_dot_product", {Int(32, 4), UInt(8, 16), Int(8, 16)}, Target::AVX512_SapphireRapids},

    {"dpwssdsx16", Int(32, 16), "saturating_dot_product", {Int(32, 16), Int(16, 32), Int(16, 32)}, Target::AVX512_SapphireRapids},
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
            fn->addFnAttr(llvm::Attribute::ReadNone);
        }
        fn->addFnAttr(llvm::Attribute::NoUnwind);
    }
}

// FIXME: This is nearly identical to CodeGen_LLVM, should re-factor this somehow.
// Only difference is the call to `optimize_x86_instructions()`
void CodeGen_X86::compile_func(const LoweredFunc &f, const std::string &simple_name,
                               const std::string &extern_name) {
    // Generate the function declaration and argument unpacking code.
    begin_func(f.linkage, simple_name, extern_name, f.args);

    // If building with MSAN, ensure that calls to halide_msan_annotate_buffer_is_initialized()
    // happen for every output buffer if the function succeeds.
    if (f.linkage != LinkageType::Internal &&
        target.has_feature(Target::MSAN)) {
        llvm::Function *annotate_buffer_fn =
            module->getFunction("halide_msan_annotate_buffer_is_initialized_as_destructor");
        internal_assert(annotate_buffer_fn)
            << "Could not find halide_msan_annotate_buffer_is_initialized_as_destructor in module\n";
        annotate_buffer_fn->addParamAttr(0, Attribute::NoAlias);
        for (const auto &arg : f.args) {
            if (arg.kind == Argument::OutputBuffer) {
                register_destructor(annotate_buffer_fn, sym_get(arg.name + ".buffer"), OnSuccess);
            }
        }
    }

    // Generate the function body.
    debug(1) << "Generating llvm bitcode for function " << f.name << "...\n";
    debug(1) << "X86: Optimizing vector instructions...\n";
    Stmt body = optimize_x86_instructions(f.body, target, this);
    debug(2) << "X86: Lowering after vector instructions:\n"
             << body << "\n\n";

    body.accept(this);

    // Clean up and return.
    end_func(f.args);
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
    if (!op->type.is_vector()) {
        // We only have peephole optimizations for vectors in here.
        CodeGen_Posix::visit(op);
        return;
    }

    if (const Call *mul = Call::as_intrinsic(op->value, {Call::widening_mul})) {
        if (op->value.type().bits() < op->type.bits() && op->type.bits() <= 32) {
            // LLVM/x86 really doesn't like 8 -> 16 bit multiplication. If we're
            // widening to 32-bits after a widening multiply, LLVM prefers to see a
            // widening multiply directly to 32-bits. This may result in extra
            // casts, so simplify to remove them.
            value = codegen(simplify(Mul::make(Cast::make(op->type, mul->args[0]), Cast::make(op->type, mul->args[1]))));
            return;
        }
    }

    CodeGen_Posix::visit(op);
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

void CodeGen_X86::visit(const VectorInstruction *op) {
    const std::string name = op->get_instruction_name();
    value = call_overloaded_intrin(op->type, name, op->args);
    internal_assert(value) << "CodeGen_X86 failed on " << Expr(op) << "\n";
}

string CodeGen_X86::mcpu_target() const {
    // Perform an ad-hoc guess for the -mcpu given features.
    // WARNING: this is used to drive -mcpu, *NOT* -mtune!
    //          The CPU choice here *WILL* affect -mattrs!
    if (target.has_feature(Target::AVX512_SapphireRapids)) {
        return "sapphirerapids";
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

    case Target::Processor::ProcessorGeneric:
        break;
    }
    internal_assert(target.processor_tune == Target::Processor::ProcessorGeneric && "The switch should be exhaustive.");
    return mcpu_target();  // Detect "best" CPU from the enabled ISA's.
}

// FIXME: we should lower everything here, instead of relying
//        that -mcpu= (`mcpu_target()`) implies/sets features for us.
string CodeGen_X86::mattrs() const {
    string features;
    string separator;
    if (target.has_feature(Target::FMA)) {
        features += "+fma";
        separator = ",";
    }
    if (target.has_feature(Target::FMA4)) {
        features += separator + "+fma4";
        separator = ",";
    }
    if (target.has_feature(Target::F16C)) {
        features += separator + "+f16c";
        separator = ",";
    }
    if (target.has_feature(Target::AVX512) ||
        target.has_feature(Target::AVX512_KNL) ||
        target.has_feature(Target::AVX512_Skylake) ||
        target.has_feature(Target::AVX512_Cannonlake)) {
        features += separator + "+avx512f,+avx512cd";
        separator = ",";
        if (target.has_feature(Target::AVX512_KNL)) {
            features += ",+avx512pf,+avx512er";
        }
        if (target.has_feature(Target::AVX512_Skylake) ||
            target.has_feature(Target::AVX512_Cannonlake)) {
            features += ",+avx512vl,+avx512bw,+avx512dq";
        }
        if (target.has_feature(Target::AVX512_Cannonlake)) {
            features += ",+avx512ifma,+avx512vbmi";
        }
        if (target.has_feature(Target::AVX512_SapphireRapids)) {
            features += ",+avx512bf16,+avx512vnni,+amx-int8,+amx-bf16";
        }
    }
    return features;
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

llvm::Type *CodeGen_X86::llvm_type_of(const Type &t) const {
    if (t.is_float() && t.bits() < 32) {
        // LLVM as of August 2019 has all sorts of issues in the x86
        // backend for half types. It injects expensive calls to
        // convert between float and half for seemingly no reason
        // (e.g. to do a select), and bitcasting to int16 doesn't
        // help, because it simplifies away the bitcast for you.
        // See: https://bugs.llvm.org/show_bug.cgi?id=43065
        // and: https://github.com/halide/Halide/issues/4166
        return llvm_type_of(t.with_code(halide_type_uint));
    } else {
        return CodeGen_Posix::llvm_type_of(t);
    }
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
