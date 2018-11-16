#include <iostream>

#include "CodeGen_X86.h"
#include "ConciseCasts.h"
#include "Debug.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "JITModule.h"
#include "LLVM_Headers.h"
#include "Param.h"
#include "Util.h"
#include "Var.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

using namespace Halide::ConciseCasts;
using namespace llvm;

CodeGen_X86::CodeGen_X86(Target t) : CodeGen_Posix(t) {

    #if !(WITH_X86)
    user_error << "x86 not enabled for this build of Halide.\n";
    #endif

    user_assert(llvm_X86_enabled) << "llvm build not configured with X86 target enabled.\n";
}

namespace {

// i32(i16_a)*i32(i16_b) +/- i32(i16_c)*i32(i16_d) can be done by
// interleaving a, c, and b, d, and then using pmaddwd. We
// recognize it here, and implement it in the initial module.
bool should_use_pmaddwd(Expr a, Expr b, vector<Expr> &result) {
    Type t = a.type();
    internal_assert(b.type() == t);

    const Mul *ma = a.as<Mul>();
    const Mul *mb = b.as<Mul>();

    if (!(ma && mb && t.is_int() && t.bits() == 32 && (t.lanes() >= 4))) {
        return false;
    }

    Type narrow = t.with_bits(16);
    vector<Expr> args = {lossless_cast(narrow, ma->a),
                         lossless_cast(narrow, ma->b),
                         lossless_cast(narrow, mb->a),
                         lossless_cast(narrow, mb->b)};
    if (!args[0].defined() || !args[1].defined() ||
        !args[2].defined() || !args[3].defined()) {
        return false;
    }

    result.swap(args);
    return true;
}

}


void CodeGen_X86::visit(const Add *op) {
    vector<Expr> matches;
    if (should_use_pmaddwd(op->a, op->b, matches)) {
        codegen(Call::make(op->type, "pmaddwd", matches, Call::Extern));
    } else {
        CodeGen_Posix::visit(op);
    }
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
        codegen(Call::make(op->type, "pmaddwd", matches, Call::Extern));
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_X86::visit(const GT *op) {
    if (op->type.is_vector()) {
        // Non-native vector widths get legalized poorly by llvm. We
        // split it up ourselves.

        Type t = op->a.type();
        int slice_size = 128 / t.bits();
        if (slice_size < t.lanes()) {
            slice_size = target.natural_vector_size(t);
        }

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
    if (op->type.is_vector()) {
        // Non-native vector widths get legalized poorly by llvm. We
        // split it up ourselves.

        Type t = op->a.type();
        int slice_size = 128 / t.bits();
        if (slice_size < t.lanes()) {
            slice_size = target.natural_vector_size(t);
        }

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
        int slice_size = 128 / t.bits();
        if (slice_size < t.lanes()) {
            slice_size = target.natural_vector_size(t);
        }

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

    vector<Expr> matches;

    struct Pattern {
        Target::Feature feature;
        bool wide_op;
        Type type;
        int min_lanes;
        string intrin;
        Expr pattern;
    };

    static Pattern patterns[] = {
        {Target::AVX2, true, Int(8, 32), 0, "llvm.x86.avx2.padds.b",
         i8_sat(wild_i16x_ + wild_i16x_)},
        {Target::FeatureEnd, true, Int(8, 16), 0, "llvm.x86.sse2.padds.b",
         i8_sat(wild_i16x_ + wild_i16x_)},
        {Target::AVX2, true, Int(8, 32), 0, "llvm.x86.avx2.psubs.b",
         i8_sat(wild_i16x_ - wild_i16x_)},
        {Target::FeatureEnd, true, Int(8, 16), 0, "llvm.x86.sse2.psubs.b",
         i8_sat(wild_i16x_ - wild_i16x_)},
#if LLVM_VERSION < 80
        // Older LLVM versions support this as an intrinsic
        {Target::AVX2, true, UInt(8, 32), 0, "llvm.x86.avx2.paddus.b",
         u8_sat(wild_u16x_ + wild_u16x_)},
        {Target::FeatureEnd, true, UInt(8, 16), 0, "llvm.x86.sse2.paddus.b",
         u8_sat(wild_u16x_ + wild_u16x_)},
        {Target::AVX2, true, UInt(8, 32), 0, "llvm.x86.avx2.psubus.b",
         u8(max(wild_i16x_ - wild_i16x_, 0))},
        {Target::FeatureEnd, true, UInt(8, 16), 0, "llvm.x86.sse2.psubus.b",
         u8(max(wild_i16x_ - wild_i16x_, 0))},
#else
        // LLVM 8.0+ require using helpers from x86.ll
        {Target::AVX2, true, UInt(8, 32), 0, "paddusbx32",
         u8_sat(wild_u16x_ + wild_u16x_)},
        {Target::FeatureEnd, true, UInt(8, 16), 0, "paddusbx16",
         u8_sat(wild_u16x_ + wild_u16x_)},
        {Target::AVX2, true, UInt(8, 32), 0, "psubusbx32",
         u8(max(wild_i16x_ - wild_i16x_, 0))},
        {Target::FeatureEnd, true, UInt(8, 16), 0, "psubusbx16",
         u8(max(wild_i16x_ - wild_i16x_, 0))},
#endif
        {Target::AVX2, true, Int(16, 16), 0, "llvm.x86.avx2.padds.w",
         i16_sat(wild_i32x_ + wild_i32x_)},
        {Target::FeatureEnd, true, Int(16, 8), 0, "llvm.x86.sse2.padds.w",
         i16_sat(wild_i32x_ + wild_i32x_)},
        {Target::AVX2, true, Int(16, 16), 0, "llvm.x86.avx2.psubs.w",
         i16_sat(wild_i32x_ - wild_i32x_)},
        {Target::FeatureEnd, true, Int(16, 8), 0, "llvm.x86.sse2.psubs.w",
         i16_sat(wild_i32x_ - wild_i32x_)},
#if LLVM_VERSION < 80
        // Older LLVM versions support this as an intrinsic
        {Target::AVX2, true, UInt(16, 16), 0, "llvm.x86.avx2.paddus.w",
         u16_sat(wild_u32x_ + wild_u32x_)},
        {Target::FeatureEnd, true, UInt(16, 8), 0, "llvm.x86.sse2.paddus.w",
         u16_sat(wild_u32x_ + wild_u32x_)},
        {Target::AVX2, true, UInt(16, 16), 0, "llvm.x86.avx2.psubus.w",
         u16(max(wild_i32x_ - wild_i32x_, 0))},
        {Target::FeatureEnd, true, UInt(16, 8), 0, "llvm.x86.sse2.psubus.w",
         u16(max(wild_i32x_ - wild_i32x_, 0))},
#else
        // LLVM 8.0+ require using helpers from x86.ll
        {Target::AVX2, true, UInt(16, 16), 0, "padduswx16",
         u16_sat(wild_u32x_ + wild_u32x_)},
        {Target::FeatureEnd, true, UInt(16, 8), 0, "padduswx8",
         u16_sat(wild_u32x_ + wild_u32x_)},
        {Target::AVX2, true, UInt(16, 16), 0, "psubuswx16",
         u16(max(wild_i32x_ - wild_i32x_, 0))},
        {Target::FeatureEnd, true, UInt(16, 8), 0, "psubuswx8",
         u16(max(wild_i32x_ - wild_i32x_, 0))},
#endif
        // Only use the avx2 version if we have > 8 lanes
        {Target::AVX2, true, Int(16, 16), 9, "llvm.x86.avx2.pmulh.w",
         i16((wild_i32x_ * wild_i32x_) / 65536)},
        {Target::AVX2, true, UInt(16, 16), 9, "llvm.x86.avx2.pmulhu.w",
         u16((wild_u32x_ * wild_u32x_) / 65536)},

        {Target::FeatureEnd, true, Int(16, 8), 0, "llvm.x86.sse2.pmulh.w",
         i16((wild_i32x_ * wild_i32x_) / 65536)},
        {Target::FeatureEnd, true, UInt(16, 8), 0, "llvm.x86.sse2.pmulhu.w",
         u16((wild_u32x_ * wild_u32x_) / 65536)},
        // LLVM 6.0+ require using helpers from x86.ll
        {Target::AVX2, true, UInt(8, 32), 0, "pavgbx32",
         u8(((wild_u16x_ + wild_u16x_) + 1) / 2)},
        {Target::FeatureEnd, true, UInt(8, 16), 0, "pavgbx16",
         u8(((wild_u16x_ + wild_u16x_) + 1) / 2)},
        {Target::AVX2, true, UInt(16, 16), 0, "pavgwx16",
         u16(((wild_u32x_ + wild_u32x_) + 1) / 2)},
        {Target::FeatureEnd, true, UInt(16, 8), 0, "pavgwx8",
         u16(((wild_u32x_ + wild_u32x_) + 1) / 2)},
        {Target::AVX2, false, Int(16, 16), 0, "packssdwx16",
         i16_sat(wild_i32x_)},
        {Target::FeatureEnd, false, Int(16, 8), 0, "packssdwx8",
         i16_sat(wild_i32x_)},
        {Target::AVX2, false, Int(8, 32), 0, "packsswbx32",
         i8_sat(wild_i16x_)},
        {Target::FeatureEnd, false, Int(8, 16), 0, "packsswbx16",
         i8_sat(wild_i16x_)},
        {Target::AVX2, false, UInt(8, 32), 0, "packuswbx32",
         u8_sat(wild_i16x_)},
        {Target::FeatureEnd, false, UInt(8, 16), 0, "packuswbx16",
         u8_sat(wild_i16x_)},
        {Target::AVX2, false, UInt(16, 16), 0, "packusdwx16",
         u16_sat(wild_i32x_)},
        {Target::SSE41, false, UInt(16, 8), 0, "packusdwx8",
         u16_sat(wild_i32x_)}
    };

    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        const Pattern &pattern = patterns[i];

        if (!target.has_feature(pattern.feature)) {
            continue;
        }

        if (op->type.lanes() < pattern.min_lanes) {
            continue;
        }

        if (expr_match(pattern.pattern, op, matches)) {
            bool match = true;
            if (pattern.wide_op) {
                // Try to narrow the matches to the target type.
                for (size_t i = 0; i < matches.size(); i++) {
                    matches[i] = lossless_cast(op->type, matches[i]);
                    if (!matches[i].defined()) match = false;
                }
            }
            if (match) {
                value = call_intrin(op->type, pattern.type.lanes(), pattern.intrin, matches);
                return;
            }
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
        Expr top_bits = cast(signed_type, op->value / 2);
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

Expr CodeGen_X86::mulhi_shr(Expr a, Expr b, int shr) {
    Type ty = a.type();
    if (ty.is_vector() && ty.bits() == 16) {
        // We can use pmulhu for this op.
        Expr p;
        if (ty.is_uint()) {
            p = u16(u32(a) * u32(b) / 65536);
        } else {
            p = i16(i32(a) * i32(b) / 65536);
        }
        if (shr) {
            p = p >> shr;
        }
        return p;
    }
    return CodeGen_Posix::mulhi_shr(a, b, shr);
}

string CodeGen_X86::mcpu() const {
    if (target.has_feature(Target::AVX512_Cannonlake)) return "cannonlake";
    if (target.has_feature(Target::AVX512_Skylake)) return "skylake-avx512";
    if (target.has_feature(Target::AVX512_KNL)) return "knl";
    if (target.has_feature(Target::AVX2)) return "haswell";
    if (target.has_feature(Target::AVX)) return "corei7-avx";
    // We want SSE4.1 but not SSE4.2, hence "penryn" rather than "corei7"
    if (target.has_feature(Target::SSE41)) return "penryn";
    // Default should not include SSSE3, hence "k8" rather than "core2"
    return "k8";
}

string CodeGen_X86::mattrs() const {
    std::string features;
    std::string separator;
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

}  // namespace Internal
}  // namespace Halide
