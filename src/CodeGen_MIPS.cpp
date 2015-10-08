#include "CodeGen_MIPS.h"
#include "Util.h"
#include "LLVM_Headers.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

CodeGen_MIPS::CodeGen_MIPS(Target t) : CodeGen_Posix(t) {
    #if !(WITH_MIPS)
    user_error << "llvm build not configured with MIPS target enabled.\n";
    #endif
    user_assert(llvm_Mips_enabled) << "llvm build not configured with MIPS target enabled.\n";
}

string CodeGen_MIPS::mcpu() const {
    if (target.bits == 32) {
        return "";
    } else {
        return "";
    }
}

string CodeGen_MIPS::mattrs() const {
    if (target.bits == 32) {
        return "";
    } else {
        return "mips64r6";
    }
}

bool CodeGen_MIPS::use_soft_float_abi() const {
    return false;
}

int CodeGen_MIPS::native_vector_bits() const {
    return 128;
}

bool CodeGen_MIPS::target_needs_software_cast_from_float16_to(Type t) const {
    // TODO: Return true on targets that need it
    return false;
}

bool CodeGen_MIPS::target_needs_software_cast_to_float16_from(Type t, RoundingMode rm) const {
    // TODO: Return true on targets that need it
    return false;
}

}}
