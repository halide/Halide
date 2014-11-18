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

void CodeGen_PNaCl::init_module() {
    CodeGen_Posix::init_module();
    module->setDataLayout("e-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-p:32:32:32-v128:32:32");
}

llvm::Triple CodeGen_PNaCl::get_target_triple() const {
    // PNaCl expects the triple le32-unknown-nacl
    llvm::Triple triple;
    triple.setArch(llvm::Triple::le32);
    triple.setVendor(llvm::Triple::UnknownVendor);
    triple.setOS(llvm::Triple::NaCl);

    return triple;
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

}}
