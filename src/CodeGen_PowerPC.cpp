#include "CodeGen_PowerPC.h"
#include "IROperator.h"
#include "IRMatch.h"
#include "Util.h"
#include "LLVM_Headers.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

CodeGen_PowerPC::CodeGen_PowerPC(Target t) : CodeGen_Posix(t) {
    #if !(WITH_POWERPC)
    user_error << "llvm build not configured with PowerPC target enabled.\n";
    #endif
    user_assert(llvm_PowerPC_enabled) << "llvm build not configured with PowerPC target enabled.\n";
}

const char* CodeGen_PowerPC::altivec_int_type_name(const Type& t) {
    if (t.is_int()) {
        switch (t.bits()) {
        case  8: return "sb";
        case 16: return "sh";
        case 32: return "sw";
        case 64: return "sd";
        }
    } else if (t.is_uint()) {
        switch (t.bits()) {
        case  8: return "ub";
        case 16: return "uh";
        case 32: return "uw";
        case 64: return "ud";
        }
    }
    return nullptr;  // not a recognized int type.
}

namespace {

Expr _i64(Expr e) {
    return cast(Int(64, e.type().lanes()), e);
}

Expr _u64(Expr e) {
    return cast(UInt(64, e.type().lanes()), e);
}
Expr _i32(Expr e) {
    return cast(Int(32, e.type().lanes()), e);
}

Expr _u32(Expr e) {
    return cast(UInt(32, e.type().lanes()), e);
}

Expr _i16(Expr e) {
    return cast(Int(16, e.type().lanes()), e);
}

Expr _u16(Expr e) {
    return cast(UInt(16, e.type().lanes()), e);
}

Expr _i8(Expr e) {
    return cast(Int(8, e.type().lanes()), e);
}

Expr _u8(Expr e) {
    return cast(UInt(8, e.type().lanes()), e);
}

Expr _f32(Expr e) {
    return cast(Float(32, e.type().lanes()), e);
}

Expr _f64(Expr e) {
    return cast(Float(64, e.type().lanes()), e);
}

}

void CodeGen_PowerPC::visit(const Cast *op) {
    if (!op->type.is_vector()) {
        // We only have peephole optimizations for vectors in here.
        CodeGen_Posix::visit(op);
        return;
    }

    vector<Expr> matches;

    struct Pattern {
        bool needs_vsx;
        bool wide_op;
        Type type;
        string intrin;
        Expr pattern;
    };

    static Pattern patterns[] = {
        {false, true, Int(8, 16), "llvm.ppc.altivec.vaddsbs",
         _i8(clamp(wild_i16x_ + wild_i16x_, -128, 127))},
        {false, true, Int(8, 16), "llvm.ppc.altivec.vsubsbs",
         _i8(clamp(wild_i16x_ - wild_i16x_, -128, 127))},
        {false, true, UInt(8, 16), "llvm.ppc.altivec.vaddubs",
         _u8(min(wild_u16x_ + wild_u16x_, 255))},
        {false, true, UInt(8, 16), "llvm.ppc.altivec.vsububs",
         _u8(max(wild_i16x_ - wild_i16x_, 0))},
        {false, true, Int(16, 8), "llvm.ppc.altivec.vaddshs",
         _i16(clamp(wild_i32x_ + wild_i32x_, -32768, 32767))},
        {false, true, Int(16, 8), "llvm.ppc.altivec.vsubshs",
         _i16(clamp(wild_i32x_ - wild_i32x_, -32768, 32767))},
        {false, true, UInt(16, 8), "llvm.ppc.altivec.vadduhs",
         _u16(min(wild_u32x_ + wild_u32x_, 65535))},
        {false, true, UInt(16, 8), "llvm.ppc.altivec.vsubuhs",
         _u16(max(wild_i32x_ - wild_i32x_, 0))},
        {false, true, Int(32, 4), "llvm.ppc.altivec.vaddsws",
         _i32(clamp(wild_i64x_ + wild_i64x_, min_i32, max_i32))},
        {false, true, Int(32, 4), "llvm.ppc.altivec.vsubsws",
         _i32(clamp(wild_i64x_ - wild_i64x_, min_i32, max_i32))},
        {false, true, UInt(32, 4), "llvm.ppc.altivec.vadduws",
         _u32(min(wild_u64x_ + wild_u64x_, max_u32))},
        {false, true, UInt(32, 4), "llvm.ppc.altivec.vsubuws",
         _u32(max(wild_i64x_ - wild_i64x_, 0))},
        {false, true, Int(8, 16), "llvm.ppc.altivec.vavgsb",
         _i8(((wild_i16x_ + wild_i16x_) + 1) / 2)},
        {false, true, UInt(8, 16), "llvm.ppc.altivec.vavgub",
         _u8(((wild_u16x_ + wild_u16x_) + 1) / 2)},
        {false, true, Int(16, 8), "llvm.ppc.altivec.vavgsh",
         _i16(((wild_i32x_ + wild_i32x_) + 1) / 2)},
        {false, true, UInt(16, 8), "llvm.ppc.altivec.vavguh",
         _u16(((wild_u32x_ + wild_u32x_) + 1) / 2)},
        {false, true, Int(32, 4), "llvm.ppc.altivec.vavgsw",
         _i32(((wild_i64x_ + wild_i64x_) + 1) / 2)},
        {false, true, UInt(32, 4), "llvm.ppc.altivec.vavguw",
         _u32(((wild_u64x_ + wild_u64x_) + 1) / 2)},
    };

    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        const Pattern &pattern = patterns[i];

        if (!target.has_feature(Target::VSX) && pattern.needs_vsx) {
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

    CodeGen_Posix::visit(op);
}

void CodeGen_PowerPC::visit(const Min *op) {
    if (!op->type.is_vector()) {
        CodeGen_Posix::visit(op);
        return;
    }
    
    bool vsx = target.has_feature(Target::VSX);
    bool arch_2_07 = target.has_feature(Target::POWER_ARCH_2_07);

    const Type& element_type = op->type.element_of();
    const char* element_type_name = altivec_int_type_name(element_type);

    if (element_type_name != nullptr &&
        (element_type.bits() < 64 || arch_2_07)) {
        value = call_intrin(op->type, (128 / element_type.bits()),
                            std::string("llvm.ppc.altivec.vmin") + element_type_name,
                            {op->a, op->b});
    } else if (op->type.element_of() == Float(32)) {
        value = call_intrin(op->type, 4, "llvm.ppc.altivec.vminfp", {op->a, op->b});
    } else if (vsx && op->type.element_of() == Float(64)) {
        value = call_intrin(op->type, 2, "llvm.ppc.vsx.xvmindp", {op->a, op->b});
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_PowerPC::visit(const Max *op) {
    if (!op->type.is_vector()) {
        CodeGen_Posix::visit(op);
        return;
    }

    bool vsx = target.has_feature(Target::VSX);
    bool arch_2_07 = target.has_feature(Target::POWER_ARCH_2_07);

    const Type& element_type = op->type.element_of();
    const char* element_type_name = altivec_int_type_name(element_type);

    if (element_type_name != nullptr &&
        (element_type.bits() < 64 || arch_2_07)) {
        value = call_intrin(op->type, (128 / element_type.bits()),
                            std::string("llvm.ppc.altivec.vmax") + element_type_name,
                            {op->a, op->b});
    } else if (op->type.element_of() == Float(32)) {
        value = call_intrin(op->type, 4, "llvm.ppc.altivec.vmaxfp", {op->a, op->b});
    } else if (vsx && op->type.element_of() == Float(64)) {
        value = call_intrin(op->type, 2, "llvm.ppc.vsx.xvmaxdp", {op->a, op->b});
    } else {
        CodeGen_Posix::visit(op);
    }
}

string CodeGen_PowerPC::mcpu() const {
    if (target.bits == 32) {
        return "ppc32";
    } else {
        if (target.has_feature(Target::POWER_ARCH_2_07))
            return "pwr8";
        else if (target.has_feature(Target::VSX))
            return "pwr7";
        else
            return "ppc64";
    }
}

string CodeGen_PowerPC::mattrs() const {
    std::string features;
    std::string separator;
    std::string enable;

    features += "+altivec";
    separator = ",";

    enable = target.has_feature(Target::VSX) ? "+" : "-";
    features += separator + enable + "vsx";
    separator = ",";

    enable = target.has_feature(Target::POWER_ARCH_2_07) ? "+" : "-";
    features += separator + enable + "power8-altivec";
    separator = ",";

    // These move instructions are defined in POWER ISA 2.06 but we do
    // not check for 2.06 currently.  So disable this for anything
    // lower than ISA 2.07
    features += separator + enable + "direct-move";
    separator = ",";

    return features;
}

bool CodeGen_PowerPC::use_soft_float_abi() const {
    return false;
}

int CodeGen_PowerPC::native_vector_bits() const {
    return 128;
}

}}
