#include "CodeGen_PNaCl.h"
#include "Util.h"
#include "LLVM_Headers.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

CodeGen_PNaCl::CodeGen_PNaCl(Target t) : CodeGen_Posix(t) {

    #if !(WITH_NATIVE_CLIENT)
    user_error << "llvm build not configured with native client enabled.\n";
    #endif

    internal_assert(t.os == Target::NaCl && t.arch == Target::PNaCl && t.bits == 32);
}

string CodeGen_PNaCl::mcpu() const {
    return "";
}

string CodeGen_PNaCl::mattrs() const {
    return "";
}

bool CodeGen_PNaCl::use_soft_float_abi() const {
    return false;
}

int CodeGen_PNaCl::native_vector_bits() const {
    return 128;
}

}}
