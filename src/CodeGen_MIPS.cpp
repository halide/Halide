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

llvm::Triple CodeGen_MIPS::get_target_triple() const {
    llvm::Triple triple;

    // Currently MIPS support is only little-endian.
    if (target.bits == 32) {
        triple.setArch(llvm::Triple::mipsel);
    } else {
        user_assert(target.bits == 64) << "Target must be 32- or 64-bit.\n";
        triple.setArch(llvm::Triple::mips64el);
    }

    if (target.os == Target::Android) {
        triple.setOS(llvm::Triple::Linux);
        triple.setEnvironment(llvm::Triple::Android);
    } else {
        user_error << "No mips support for this OS\n";
    }

    return triple;
}

void CodeGen_MIPS::compile(Stmt stmt, string name,
                          const vector<Argument> &args,
                          const vector<Buffer> &images_to_embed) {
    init_module();

    module = get_initial_module_for_target(target, context);

    // Fix the target triple.
    debug(1) << "Target triple of initial module: " << module->getTargetTriple() << "\n";

    llvm::Triple triple = get_target_triple();
    module->setTargetTriple(triple.str());

    debug(1) << "Target triple of initial module: " << module->getTargetTriple() << "\n";

    // Pass to the generic codegen
    CodeGen::compile(stmt, name, args, images_to_embed);

    // Optimize
    CodeGen::optimize_module();
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

}}
