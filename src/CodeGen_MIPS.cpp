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
        triple.setEnvironment(llvm::Triple::EABI);
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

    #if (WITH_MIPS)

    // TODO: Remove or handle vectorized code in the stmt

    init_module();

    module = get_initial_module_for_target(target, context);

    // MIPS expects the triple le32-unknown-nacl
    llvm::Triple triple;
    triple.setArch(llvm::Triple::le32);
    triple.setVendor(llvm::Triple::UnknownVendor);
    triple.setOS(llvm::Triple::NaCl);
    module->setTargetTriple(triple.str());
    module->setDataLayout("e-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-p:32:32:32-v128:32:32");

    // Pass to the generic codegen
    CodeGen::compile(stmt, name, args, images_to_embed);

    // Optimize
    CodeGen::optimize_module();
    #endif
}

string CodeGen_MIPS::mcpu() const {
    return "";
}

string CodeGen_MIPS::mattrs() const {
    return "";
}

bool CodeGen_MIPS::use_soft_float_abi() const {
    return false;
}

}}
