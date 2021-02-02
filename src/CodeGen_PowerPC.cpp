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

CodeGen_PowerPC::CodeGen_PowerPC(const Target &t)
    : CodeGen_Posix(t) {
#if !defined(WITH_POWERPC)
    user_error << "llvm build not configured with PowerPC target enabled.\n";
#endif
    user_assert(llvm_PowerPC_enabled) << "llvm build not configured with PowerPC target enabled.\n";
}

namespace {

const int max_intrinsic_args = 4;

struct PowerPCIntrinsic {
    const char *intrin_name;
    halide_type_t ret_type;
    const char *name;
    halide_type_t arg_types[max_intrinsic_args];
    Target::Feature feature = Target::FeatureEnd;
};

// clang-format off
const PowerPCIntrinsic intrinsic_defs[] = {
    {"llvm.ppc.altivec.vminsb", Int(8, 16), "min", {Int(8, 16), Int(8, 16)}},
    {"llvm.ppc.altivec.vminub", UInt(8, 16), "min", {UInt(8, 16), UInt(8, 16)}},
    {"llvm.ppc.altivec.vminsh", Int(16, 8), "min", {Int(16, 8), Int(16, 8)}},
    {"llvm.ppc.altivec.vminuh", UInt(16, 8), "min", {UInt(16, 8), UInt(16, 8)}},
    {"llvm.ppc.altivec.vminsw", Int(32, 4), "min", {Int(32, 4), Int(32, 4)}},
    {"llvm.ppc.altivec.vminuw", UInt(32, 4), "min", {UInt(32, 4), UInt(32, 4)}},
    {"llvm.ppc.altivec.vminfp", Float(32, 4), "min", {Float(32, 4), Float(32, 4)}},
    {"llvm.ppc.altivec.vminsd", Int(64, 2), "min", {Int(64, 2), Int(64, 2)}, Target::POWER_ARCH_2_07},
    {"llvm.ppc.altivec.vminud", UInt(64, 2), "min", {UInt(64, 2), UInt(64, 2)}, Target::POWER_ARCH_2_07},
    {"llvm.ppc.vsx.xvmindp", Float(64, 2), "min", {Float(64, 2), Float(64, 2)}, Target::VSX},

    {"llvm.ppc.altivec.vmaxsb", Int(8, 16), "max", {Int(8, 16), Int(8, 16)}},
    {"llvm.ppc.altivec.vmaxub", UInt(8, 16), "max", {UInt(8, 16), UInt(8, 16)}},
    {"llvm.ppc.altivec.vmaxsh", Int(16, 8), "max", {Int(16, 8), Int(16, 8)}},
    {"llvm.ppc.altivec.vmaxuh", UInt(16, 8), "max", {UInt(16, 8), UInt(16, 8)}},
    {"llvm.ppc.altivec.vmaxsw", Int(32, 4), "max", {Int(32, 4), Int(32, 4)}},
    {"llvm.ppc.altivec.vmaxuw", UInt(32, 4), "max", {UInt(32, 4), UInt(32, 4)}},
    {"llvm.ppc.altivec.vmaxfp", Float(32, 4), "max", {Float(32, 4), Float(32, 4)}},
    {"llvm.ppc.altivec.vmaxsd", Int(64, 2), "max", {Int(64, 2), Int(64, 2)}, Target::POWER_ARCH_2_07},
    {"llvm.ppc.altivec.vmaxud", UInt(64, 2), "max", {UInt(64, 2), UInt(64, 2)}, Target::POWER_ARCH_2_07},
    {"llvm.ppc.vsx.xvmaxdp", Float(64, 2), "max", {Float(64, 2), Float(64, 2)}, Target::VSX},

    {"llvm.ppc.altivec.vaddsbs", Int(8, 16), "saturating_add", {Int(8, 16), Int(8, 16)}},
    {"llvm.ppc.altivec.vaddubs", UInt(8, 16), "saturating_add", {UInt(8, 16), UInt(8, 16)}},
    {"llvm.ppc.altivec.vaddshs", Int(16, 8), "saturating_add", {Int(16, 8), Int(16, 8)}},
    {"llvm.ppc.altivec.vadduhs", UInt(16, 8), "saturating_add", {UInt(16, 8), UInt(16, 8)}},
    {"llvm.ppc.altivec.vaddsws", Int(32, 4), "saturating_add", {Int(32, 4), Int(32, 4)}},
    {"llvm.ppc.altivec.vadduws", UInt(32, 4), "saturating_add", {UInt(32, 4), UInt(32, 4)}},

    {"llvm.ppc.altivec.vsubsbs", Int(8, 16), "saturating_sub", {Int(8, 16), Int(8, 16)}},
    {"llvm.ppc.altivec.vsububs", UInt(8, 16), "saturating_sub", {UInt(8, 16), UInt(8, 16)}},
    {"llvm.ppc.altivec.vsubshs", Int(16, 8), "saturating_sub", {Int(16, 8), Int(16, 8)}},
    {"llvm.ppc.altivec.vsubuhs", UInt(16, 8), "saturating_sub", {UInt(16, 8), UInt(16, 8)}},
    {"llvm.ppc.altivec.vsubsws", Int(32, 4), "saturating_sub", {Int(32, 4), Int(32, 4)}},
    {"llvm.ppc.altivec.vsubuws", UInt(32, 4), "saturating_sub", {UInt(32, 4), UInt(32, 4)}},

    {"llvm.ppc.altivec.vavgsb", Int(8, 16), "rounding_halving_add", {Int(8, 16), Int(8, 16)}},
    {"llvm.ppc.altivec.vavgub", UInt(8, 16), "rounding_halving_add", {UInt(8, 16), UInt(8, 16)}},
    {"llvm.ppc.altivec.vavgsh", Int(16, 8), "rounding_halving_add", {Int(16, 8), Int(16, 8)}},
    {"llvm.ppc.altivec.vavguh", UInt(16, 8), "rounding_halving_add", {UInt(16, 8), UInt(16, 8)}},
    {"llvm.ppc.altivec.vavgsw", Int(32, 4), "rounding_halving_add", {Int(32, 4), Int(32, 4)}},
    {"llvm.ppc.altivec.vavguw", UInt(32, 4), "rounding_halving_add", {UInt(32, 4), UInt(32, 4)}},
};
// clang-format on

}  // namespace

void CodeGen_PowerPC::init_module() {
    CodeGen_Posix::init_module();

    for (const PowerPCIntrinsic &i : intrinsic_defs) {
        if (i.feature != Target::FeatureEnd && !target.has_feature(i.feature)) {
            continue;
        }

        Type ret_type = i.ret_type;
        std::vector<Type> arg_types;
        arg_types.reserve(max_intrinsic_args);
        for (halide_type_t j : i.arg_types) {
            if (j.bits == 0) {
                break;
            }
            arg_types.emplace_back(j);
        }

        declare_intrin_overload(i.name, ret_type, i.intrin_name, std::move(arg_types));
    }
}

void CodeGen_PowerPC::visit(const Min *op) {
    if (op->type.is_vector()) {
        value = call_overloaded_intrin(op->type, "min", {op->a, op->b});
        if (value) {
            return;
        }
    }
    return CodeGen_Posix::visit(op);
}

void CodeGen_PowerPC::visit(const Max *op) {
    if (op->type.is_vector()) {
        value = call_overloaded_intrin(op->type, "max", {op->a, op->b});
        if (value) {
            return;
        }
    }
    return CodeGen_Posix::visit(op);
}

string CodeGen_PowerPC::mcpu() const {
    if (target.bits == 32) {
        return "ppc32";
    } else {
        if (target.has_feature(Target::POWER_ARCH_2_07)) {
            return "pwr8";
        } else if (target.has_feature(Target::VSX)) {
            return "pwr7";
        } else {
            return "ppc64";
        }
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
