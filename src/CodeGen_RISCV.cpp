#include "CodeGen_RISCV.h"
#include "LLVM_Headers.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::string;

using namespace llvm;

CodeGen_RISCV::CodeGen_RISCV(const Target &t)
    : CodeGen_Posix(t) {
#if !defined(WITH_RISCV)
    user_error << "llvm build not configured with RISCV target enabled.\n";
#endif
}

string CodeGen_RISCV::mcpu() const {
    return "";
}

string CodeGen_RISCV::mattrs() const {
    string arch_flags = "+m,+a,+f,+d,+c";
    if (target.has_feature(Target::RVV)) {
        arch_flags += ",+experimental-v";
    }
    return arch_flags;
}

string CodeGen_RISCV::mabi() const {
    string abi;
    if (target.bits == 32) {
        abi = "lp32";
    } else {
        abi = "lp64";
    }
    if (!target.has_feature(Target::SoftFloatABI)) {
        abi += "d";
    }
    return abi;
}

bool CodeGen_RISCV::use_soft_float_abi() const {
    return target.has_feature(Target::SoftFloatABI);
}

int CodeGen_RISCV::native_vector_bits() const {
    return 128;
}

}  // namespace Internal
}  // namespace Halide
