#include "CodeGen_Posix.h"
#include "ConciseCasts.h"
#include "Debug.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "LLVM_Headers.h"
#include "Simplify.h"
#include "Util.h"

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
    string mcpu() const override;
    string mattrs() const override;
    bool use_soft_float_abi() const override;
    int native_vector_bits() const override;

    int vector_lanes_for_slice(const Type &t) const;

    llvm::Type *llvm_type_of(const Type &t) const override;

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

    // LLVM 6.0+ require using helpers from x86.ll, x86_avx.ll
    {"pavgbx32", UInt(8, 32), "rounding_halving_add", {UInt(8, 32), UInt(8, 32)}, Target::AVX2},
    {"pavgbx16", UInt(8, 16), "rounding_halving_add", {UInt(8, 16), UInt(8, 16)}},
    {"pavgwx16", UInt(16, 16), "rounding_halving_add", {UInt(16, 16), UInt(16, 16)}, Target::AVX2},
    {"pavgwx8", UInt(16, 8), "rounding_halving_add", {UInt(16, 8), UInt(16, 8)}},

    {"packssdwx16", Int(16, 16), "saturating_narrow", {Int(32, 16)}, Target::AVX2},
    {"packssdwx8", Int(16, 8), "saturating_narrow", {Int(32, 8)}},
    {"packsswbx32", Int(8, 32), "saturating_narrow", {Int(16, 32)}, Target::AVX2},
    {"packsswbx16", Int(8, 16), "saturating_narrow", {Int(16, 16)}},
    {"packusdwx16", UInt(16, 16), "saturating_narrow", {Int(32, 16)}, Target::AVX2},
    {"packusdwx8", UInt(16, 8), "saturating_narrow", {Int(32, 8)}, Target::SSE41},
    {"packuswbx32", UInt(8, 32), "saturating_narrow", {Int(16, 32)}, Target::AVX2},
    {"packuswbx16", UInt(8, 16), "saturating_narrow", {Int(16, 16)}},

    // Multiply keep high half
    {"llvm.x86.avx2.pmulh.w", Int(16, 16), "pmulh", {Int(16, 16), Int(16, 16)}, Target::AVX2},
    {"llvm.x86.avx2.pmulhu.w", UInt(16, 16), "pmulh", {UInt(16, 16), UInt(16, 16)}, Target::AVX2},
    {"llvm.x86.avx2.pmul.hr.sw", Int(16, 16), "pmulhr", {Int(16, 16), Int(16, 16)}, Target::AVX2},
    {"llvm.x86.sse2.pmulh.w", Int(16, 8), "pmulh", {Int(16, 8), Int(16, 8)}},
    {"llvm.x86.sse2.pmulhu.w", UInt(16, 8), "pmulh", {UInt(16, 8), UInt(16, 8)}},
    {"llvm.x86.ssse3.pmul.hr.sw.128", Int(16, 8), "pmulhr", {Int(16, 8), Int(16, 8)}, Target::SSE41},

    // Pairwise multiply-add
    {"llvm.x86.avx512.pmaddw.d.512", Int(32, 16), "pmaddwd", {Int(16, 32), Int(16, 32)}, Target::AVX512_Skylake},
    {"llvm.x86.avx512.pmaddw.d.512", Int(32, 16), "pmaddwd", {Int(16, 32), Int(16, 32)}, Target::AVX512_Cannonlake},
    {"llvm.x86.avx2.pmadd.wd", Int(32, 8), "pmaddwd", {Int(16, 16), Int(16, 16)}, Target::AVX2},
    {"llvm.x86.sse2.pmadd.wd", Int(32, 4), "pmaddwd", {Int(16, 8), Int(16, 8)}},

    // Convert FP32 to BF16
    {"vcvtne2ps2bf16x32", BFloat(16, 32), "f32_to_bf16", {Float(32, 32)}, Target::AVX512_SapphireRapids},
    {"llvm.x86.avx512bf16.cvtneps2bf16.512", BFloat(16, 16), "f32_to_bf16", {Float(32, 16)}, Target::AVX512_SapphireRapids},
    {"llvm.x86.avx512bf16.cvtneps2bf16.256", BFloat(16, 8), "f32_to_bf16", {Float(32, 8)}, Target::AVX512_SapphireRapids},
    // LLVM does not provide an unmasked 128bit cvtneps2bf16 intrinsic, so provide a wrapper around the masked version.
    {"vcvtneps2bf16x4", BFloat(16, 4), "f32_to_bf16", {Float(32, 4)}, Target::AVX512_SapphireRapids},

    // Dot product vector reduction
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

    {"tileloadd64_i8", Int(8, 1024), "tile_load", {Int(16), Int(16), Handle(), Int(64), Int(64)}, Target::AVX512_SapphireRapids, x86Intrinsic::AccessesMemory},
    {"tdpbssd", Int(32, 256), "tile_matmul", {Int(16), Int(16), Int(16), Int(32, 256), Int(8, 1024), Int(8, 1024)},  Target::AVX512_SapphireRapids},
    {"tilezero_i32", Int(32, 256), "tile_zero", {Int(16), Int(16)},  Target::AVX512_SapphireRapids},
    // CodeGen_LLVM cannot cope with returning Type() ie void*, and return type needs to be vector to trigger call_overloaded_intrin
    {"tilestored64", Bool(2), "tile_store", {Int(16), Int(16), Handle(), Int(64), Int(64), Int(32, 256)}, Target::AVX512_SapphireRapids, x86Intrinsic::AccessesMemory},

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

// i32(i16_a)*i32(i16_b) +/- i32(i16_c)*i32(i16_d) can be done by
// interleaving a, c, and b, d, and then using pmaddwd. We
// recognize it here, and implement it in the initial module.
bool should_use_pmaddwd(const Expr &a, const Expr &b, vector<Expr> &result) {
    Type t = a.type();
    internal_assert(b.type() == t);

    if (!(t.is_int() && t.bits() == 32 && t.lanes() >= 4)) {
        return false;
    }

    const Call *ma = Call::as_intrinsic(a, {Call::widening_mul});
    const Call *mb = Call::as_intrinsic(b, {Call::widening_mul});
    // pmaddwd can't handle mixed type widening muls.
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
    if (should_use_pmaddwd(op->a, op->b, matches)) {
        Expr ac = Shuffle::make_interleave({matches[0], matches[2]});
        Expr bd = Shuffle::make_interleave({matches[1], matches[3]});
        value = call_overloaded_intrin(op->type, "pmaddwd", {ac, bd});
        if (value) {
            return;
        }
    }
    CodeGen_Posix::visit(op);
}

void CodeGen_X86::visit(const Sub *op) {
    vector<Expr> matches;
    if (should_use_pmaddwd(op->a, op->b, matches)) {
        // Negate one of the factors in the second expression
        if (is_const(matches[2])) {
            matches[2] = -matches[2];
        } else {
            matches[3] = -matches[3];
        }
        Expr ac = Shuffle::make_interleave({matches[0], matches[2]});
        Expr bd = Shuffle::make_interleave({matches[1], matches[3]});
        value = call_overloaded_intrin(op->type, "pmaddwd", {ac, bd});
        if (value) {
            return;
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

    if (!op->type.is_vector()) {
        // We only have peephole optimizations for vectors in here.
        CodeGen_Posix::visit(op);
        return;
    }

    struct Pattern {
        string intrin;
        Expr pattern;
    };

    // clang-format off
    static Pattern patterns[] = {
        {"pmulh", i16(widening_mul(wild_i16x_, wild_i16x_) >> u32(16))},
        {"pmulh", u16(widening_mul(wild_u16x_, wild_u16x_) >> u32(16))},
        {"pmulhr", i16(rounding_shift_right(widening_mul(wild_i16x_, wild_i16x_), u32(15)))},

        {"saturating_narrow", i16_sat(wild_i32x_)},
        {"saturating_narrow", u16_sat(wild_i32x_)},
        {"saturating_narrow", i8_sat(wild_i16x_)},
        {"saturating_narrow", u8_sat(wild_i16x_)},

        {"f32_to_bf16", bf16(wild_f32x_)},
    };
    // clang-format on

    vector<Expr> matches;
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        const Pattern &pattern = patterns[i];
        if (expr_match(pattern.pattern, op, matches)) {
            value = call_overloaded_intrin(op->type, pattern.intrin, matches);
            if (value) {
                return;
            }
        }
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

    // Workaround for https://llvm.org/bugs/show_bug.cgi?id=24512
    // LLVM uses a numerically unstable method for vector
    // uint32->float conversion before AVX.
    if (op->value.type().element_of() == UInt(32) &&
        op->type.is_float() &&
        op->type.is_vector() &&
        !target.has_feature(Target::AVX)) {
        Type signed_type = Int(32, op->type.lanes());

        // Convert the top 31 bits to float using the signed version
        Expr top_bits = cast(signed_type, op->value >> 1);
        top_bits = cast(op->type, top_bits);

        // Convert the bottom bit
        Expr bottom_bit = cast(signed_type, op->value % 2);
        bottom_bit = cast(op->type, bottom_bit);

        // Recombine as floats
        codegen(top_bits + top_bits + bottom_bit);
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_X86::visit(const Call *op) {
#if LLVM_VERSION < 110
    if (op->is_intrinsic(Call::widening_mul) && (op->type.is_int() || op->type.is_uint())) {
        // Widening integer multiply of non-power-of-two vector sizes is
        // broken in older llvms for older x86:
        // https://bugs.llvm.org/show_bug.cgi?id=44976
        const int lanes = op->type.lanes();
        if (!target.has_feature(Target::SSE41) &&
            (lanes & (lanes - 1)) &&
            (op->type.bits() >= 32) &&
            !op->type.is_float()) {
            // Any fancy shuffles to pad or slice into smaller vectors
            // just gets undone by LLVM and retriggers the bug. Just
            // scalarize.
            vector<Expr> result;
            for (int i = 0; i < lanes; i++) {
                result.emplace_back(Shuffle::make_extract_element(Cast::make(op->type, op->args[0]), i) *
                                    Shuffle::make_extract_element(Cast::make(op->type, op->args[1]), i));
            }
            codegen(Shuffle::make_concat(result));
            return;
        }
    }
#endif
    if (op->is_intrinsic(Call::mulhi_shr)) {
        internal_assert(op->args.size() == 3);

        Expr p_wide = widening_mul(op->args[0], op->args[1]);
        const UIntImm *shift = op->args[2].as<UIntImm>();
        internal_assert(shift != nullptr) << "Third argument to mulhi_shr intrinsic must be an unsigned integer immediate.\n";
        value = codegen(cast(op->type, p_wide >> op->type.bits()) >> shift->value);
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_X86::codegen_vector_reduce(const VectorReduce *op, const Expr &init) {
    if (op->op != VectorReduce::Add) {
        CodeGen_Posix::codegen_vector_reduce(op, init);
        return;
    }
    const int factor = op->value.type().lanes() / op->type.lanes();

    struct Pattern {
        int factor;
        Expr pattern;
        const char *intrin;
        Type narrow_type;
        uint32_t flags = 0;
        enum {
            CombineInit = 1 << 0,
            SwapOperands = 1 << 1,
        };
    };
    // clang-format off
    static const Pattern patterns[] = {
        {2, wild_f32x_ * wild_f32x_, "dot_product", BFloat(16), Pattern::CombineInit},
        {2, i32(widening_mul(wild_i16x_, wild_i16x_)), "dot_product", {}, Pattern::CombineInit},
        {4, i32(widening_mul(wild_u8x_, wild_i8x_)), "dot_product", {}, Pattern::CombineInit},
        {4, i32(widening_mul(wild_i8x_, wild_u8x_)), "dot_product", {}, Pattern::CombineInit | Pattern::SwapOperands},
        {2, i32(widening_mul(wild_i16x_, wild_i16x_)), "pmaddwd", Int(16)},
        {2, i32(widening_mul(wild_i8x_, wild_i8x_)), "pmaddwd", Int(16)},
        {2, i32(widening_mul(wild_i8x_, wild_u8x_)), "pmaddwd", Int(16)},
        {2, i32(widening_mul(wild_u8x_, wild_i8x_)), "pmaddwd", Int(16)},
        {2, i32(widening_mul(wild_u8x_, wild_u8x_)), "pmaddwd", Int(16)},
        // One could do a horizontal widening addition with
        // pmaddwd against a vector of ones. Currently disabled
        // because I haven't found case where it's clearly better.
    };
    // clang-format on

    std::vector<Expr> matches;
    for (const Pattern &p : patterns) {
        if (p.factor != factor) {
            continue;
        }
        if (expr_match(p.pattern, op->value, matches)) {
            Expr a = matches[0];
            Expr b = matches[1];
            if (p.flags & Pattern::SwapOperands) {
                std::swap(a, b);
            }
            if (p.narrow_type.bits() > 0) {
                a = lossless_cast(p.narrow_type.with_lanes(a.type().lanes()), a);
                b = lossless_cast(p.narrow_type.with_lanes(b.type().lanes()), b);
            }
            if (!a.defined() || !b.defined()) { continue; }

            if (p.flags & Pattern::CombineInit) {
                value = call_overloaded_intrin(op->type, p.intrin, {init, a, b});
                if (value) { return; }
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
        LoadInst *load = builder->CreateAlignedLoad(ptr, llvm::Align(op->type.bytes()));
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

string CodeGen_X86::mcpu() const {
    if (target.has_feature(Target::AVX512_SapphireRapids)) {
#if LLVM_VERSION >= 120
        return "sapphirerapids";
#else
        user_error << "AVX512 SapphireRapids requires LLVM 12 or later.";
        return "";
#endif
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
#if LLVM_VERSION >= 120
            features += ",+avx512bf16,+avx512vnni";
#else
            user_error << "AVX512 SapphireRapids requires LLVM 12 or later.";
#endif
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
