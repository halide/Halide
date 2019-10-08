#include "CodeGen_RISCV.h"
#include "LLVM_Headers.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::string;

using namespace llvm;

CodeGen_RISCV::CodeGen_RISCV(Target t)
    : CodeGen_Posix(t) {
#if !defined(WITH_RISCV)
    user_error << "llvm build not configured with RISCV target enabled.\n";
#endif
}

string CodeGen_RISCV::mcpu() const {
    return "";
}

string CodeGen_RISCV::mattrs() const {
    return "";
}

bool CodeGen_RISCV::use_soft_float_abi() const {
    return false;
}

int CodeGen_RISCV::native_vector_bits() const {
    return 128;
}

}  // namespace Internal
}  // namespace Halide
