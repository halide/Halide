#include "CodeGen_WebAssembly.h"
#include "Util.h"
#include "LLVM_Headers.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

CodeGen_WebAssembly::CodeGen_WebAssembly(Target t) : CodeGen_Posix(t) {
    #if !(WITH_WEBASSEMBLY)
    user_error << "llvm build not configured with WebAssembly target enabled.\n";
    #endif
    user_assert(llvm_WebAssembly_enabled) << "llvm build not configured with WebAssembly target enabled.\n";
}

string CodeGen_WebAssembly::mcpu() const {
    if (target.bits == 32) {
        return "";
    } else {
        return "";
    }
}

string CodeGen_WebAssembly::mattrs() const {
    return "";
}

bool CodeGen_WebAssembly::use_soft_float_abi() const {
    return false;
}

int CodeGen_WebAssembly::native_vector_bits() const {
    return 128;
}

}}
