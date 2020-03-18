#include "CodeGen_PowerPC.h"
#include "ConciseCasts.h"
#include "IRMatch.h"
#include "IROperator.h"
#include "LLVM_Headers.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

using namespace Halide::ConciseCasts;
using namespace llvm;

CodeGen_PowerPC::CodeGen_PowerPC(Target t)
    : CodeGen_Posix(t) {
#if !defined(WITH_POWERPC)
    user_error << "llvm build not configured with PowerPC target enabled.\n";
#endif
    user_assert(llvm_PowerPC_enabled) << "llvm build not configured with PowerPC target enabled.\n";
}

const char *CodeGen_PowerPC::altivec_int_type_name(const Type &t) {
    if (t.is_int()) {
        switch (t.bits()) {
        case 8:
            return "sb";
        case 16:
            return "sh";
        case 32:
            return "sw";
        case 64:
            return "sd";
        }
    } else if (t.is_uint()) {
        switch (t.bits()) {
        case 8:
            return "ub";
        case 16:
            return "uh";
        case 32:
            return "uw";
        case 64:
            return "ud";
        }
    }
    return nullptr;  // not a recognized int type.
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
         i8_sat(wild_i16x_ + wild_i16x_)},
        {false, true, Int(8, 16), "llvm.ppc.altivec.vsubsbs",
         i8_sat(wild_i16x_ - wild_i16x_)},
        {false, true, UInt(8, 16), "llvm.ppc.altivec.vaddubs",
         u8_sat(wild_u16x_ + wild_u16x_)},
        {false, true, UInt(8, 16), "llvm.ppc.altivec.vsububs",
         u8(max(wild_i16x_ - wild_i16x_, 0))},
        {false, true, Int(16, 8), "llvm.ppc.altivec.vaddshs",
         i16_sat(wild_i32x_ + wild_i32x_)},
        {false, true, Int(16, 8), "llvm.ppc.altivec.vsubshs",
         i16_sat(wild_i32x_ - wild_i32x_)},
        {false, true, UInt(16, 8), "llvm.ppc.altivec.vadduhs",
         u16_sat(wild_u32x_ + wild_u32x_)},
        {false, true, UInt(16, 8), "llvm.ppc.altivec.vsubuhs",
         u16(max(wild_i32x_ - wild_i32x_, 0))},
        {false, true, Int(32, 4), "llvm.ppc.altivec.vaddsws",
         i32_sat(wild_i64x_ + wild_i64x_)},
        {false, true, Int(32, 4), "llvm.ppc.altivec.vsubsws",
         i32_sat(wild_i64x_ - wild_i64x_)},
        {false, true, UInt(32, 4), "llvm.ppc.altivec.vadduws",
         u32_sat(wild_u64x_ + wild_u64x_)},
        {false, true, UInt(32, 4), "llvm.ppc.altivec.vsubuws",
         u32(max(wild_i64x_ - wild_i64x_, 0))},
        {false, true, Int(8, 16), "llvm.ppc.altivec.vavgsb",
         i8(((wild_i16x_ + wild_i16x_) + 1) / 2)},
        {false, true, UInt(8, 16), "llvm.ppc.altivec.vavgub",
         u8(((wild_u16x_ + wild_u16x_) + 1) / 2)},
        {false, true, Int(16, 8), "llvm.ppc.altivec.vavgsh",
         i16(((wild_i32x_ + wild_i32x_) + 1) / 2)},
        {false, true, UInt(16, 8), "llvm.ppc.altivec.vavguh",
         u16(((wild_u32x_ + wild_u32x_) + 1) / 2)},
        {false, true, Int(32, 4), "llvm.ppc.altivec.vavgsw",
         i32(((wild_i64x_ + wild_i64x_) + 1) / 2)},
        {false, true, UInt(32, 4), "llvm.ppc.altivec.vavguw",
         u32(((wild_u64x_ + wild_u64x_) + 1) / 2)},
    };

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
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

    const Type &element_type = op->type.element_of();
    const char *element_type_name = altivec_int_type_name(element_type);

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

    const Type &element_type = op->type.element_of();
    const char *element_type_name = altivec_int_type_name(element_type);

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

}  // namespace Internal
}  // namespace Halide
