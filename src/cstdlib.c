#include <stdio.h>
#include <stdlib.h>

#include <caml/mlvalues.h>
#include <caml/alloc.h>
#include <caml/custom.h>
#include <caml/memory.h>
#include <caml/fail.h>
#include <caml/callback.h>

#include <llvm-c/Core.h>

#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Type.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Instructions.h>
#include <llvm/Intrinsics.h>
#include <llvm/Linker.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/ADT/Triple.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Bitcode/ReaderWriter.h>

// <stolen from ISPC>

/** In many of the builtins-*.ll files, we have declarations of various LLVM
    intrinsics that are then used in the implementation of various target-
    specific functions.  This function loops over all of the intrinsic 
    declarations and makes sure that the signature we have in our .ll file
    matches the signature of the actual intrinsic.
*/
static void
lCheckModuleIntrinsics(llvm::LLVMContext* ctx, llvm::Module *module) {
    llvm::Module::iterator iter;
    for (iter = module->begin(); iter != module->end(); ++iter) {
        llvm::Function *func = iter;
        if (!func->isIntrinsic())
            continue;

        const std::string funcName = func->getName().str();
        // Work around http://llvm.org/bugs/show_bug.cgi?id=10438; only
        // check the llvm.x86.* intrinsics for now...
        if (!strncmp(funcName.c_str(), "llvm.x86.", 9)) {
            llvm::Intrinsic::ID id = (llvm::Intrinsic::ID)func->getIntrinsicID();
            assert(id != 0);
            llvm::Type *intrinsicType = 
                llvm::Intrinsic::getType(*ctx, id);
            intrinsicType = llvm::PointerType::get(intrinsicType, 0);
            assert(func->getType() == intrinsicType);
        }
    }
}


/** This utility function takes serialized binary LLVM bitcode and adds its
    definitions to the given module.  Functions in the bitcode that can be
    mapped to ispc functions are also added to the symbol table.

    @param bitcode     Binary LLVM bitcode (e.g. the contents of a *.bc file)
    @param length      Length of the bitcode buffer
    @param module      Module to link the bitcode into
    @param symbolTable Symbol table to add definitions to
 */
static void
AddBitcodeToModule(const unsigned char *bitcode, int length, llvm::LLVMContext* ctx, llvm::Module *module) {
    std::string bcErr;
    llvm::StringRef sb = llvm::StringRef((char *)bitcode, length);
    llvm::MemoryBuffer *bcBuf = llvm::MemoryBuffer::getMemBuffer(sb);
    llvm::Module *bcModule = llvm::ParseBitcodeFile(bcBuf, *ctx, &bcErr);
    if (!bcModule) {
        fprintf(stderr, "Error parsing stdlib bitcode: %s", bcErr.c_str());
        exit(1);
    }
    else {
        // FIXME: this feels like a bad idea, but the issue is that when we
        // set the llvm::Module's target triple in the ispc Module::Module
        // constructor, we start by calling llvm::sys::getHostTriple() (and
        // then change the arch if needed).  Somehow that ends up giving us
        // strings like 'x86_64-apple-darwin11.0.0', while the stuff we
        // compile to bitcode with clang has module triples like
        // 'i386-apple-macosx10.7.0'.  And then LLVM issues a warning about
        // linking together modules with incompatible target triples..
        llvm::Triple mTriple(module->getTargetTriple());
        llvm::Triple bcTriple(bcModule->getTargetTriple());
        assert(bcTriple.getArch() == llvm::Triple::UnknownArch ||
               mTriple.getArch() == bcTriple.getArch());
        assert(bcTriple.getVendor() == llvm::Triple::UnknownVendor ||
               mTriple.getVendor() == bcTriple.getVendor());
        bcModule->setTargetTriple(mTriple.str());

        std::string(linkError);
        if (llvm::Linker::LinkModules(module, bcModule, 
                                      llvm::Linker::DestroySource,
                                      &linkError)) {
            fprintf(stderr, "Error linking stdlib bitcode: %s", linkError.c_str());
            exit(1);
        }
        lCheckModuleIntrinsics(ctx, module);
    }
}

// </stolen from ISPC>

extern "C" {

    /* llmodule -> unit */
    CAMLprim value init_module_ptx(LLVMModuleRef mod) {
        LLVMContextRef ctx = LLVMGetModuleContext(mod);
        extern unsigned char builtins_bitcode_ptx[];
        extern int builtins_bitcode_ptx_length;
        AddBitcodeToModule(builtins_bitcode_ptx, builtins_bitcode_ptx_length, llvm::unwrap(ctx), llvm::unwrap(mod));
        return Val_unit;
    }

    CAMLprim value init_module_ptx_dev(LLVMModuleRef mod) {
        LLVMContextRef ctx = LLVMGetModuleContext(mod);
        extern unsigned char builtins_bitcode_ptx_dev[];
        extern int builtins_bitcode_ptx_dev_length;
        AddBitcodeToModule(builtins_bitcode_ptx_dev, builtins_bitcode_ptx_dev_length, llvm::unwrap(ctx), llvm::unwrap(mod));
        return Val_unit;
    }

    CAMLprim value init_module_x86(LLVMModuleRef mod) {
        LLVMContextRef ctx = LLVMGetModuleContext(mod);
        extern unsigned char builtins_bitcode_x86[];
        extern int builtins_bitcode_x86_length;
        AddBitcodeToModule(builtins_bitcode_x86, builtins_bitcode_x86_length, llvm::unwrap(ctx), llvm::unwrap(mod));
        return Val_unit;        
    }

    CAMLprim value init_module_x86_avx(LLVMModuleRef mod) {
        LLVMContextRef ctx = LLVMGetModuleContext(mod);
        extern unsigned char builtins_bitcode_x86_avx[];
        extern int builtins_bitcode_x86_avx_length;
        AddBitcodeToModule(builtins_bitcode_x86_avx, builtins_bitcode_x86_avx_length, llvm::unwrap(ctx), llvm::unwrap(mod));
        return Val_unit;        
    }

    CAMLprim value init_module_arm(LLVMModuleRef mod) {
        LLVMContextRef ctx = LLVMGetModuleContext(mod);
        extern unsigned char builtins_bitcode_arm[];
        extern int builtins_bitcode_arm_length;
        AddBitcodeToModule(builtins_bitcode_arm, builtins_bitcode_arm_length, llvm::unwrap(ctx), llvm::unwrap(mod));
        return Val_unit;        
    }

    CAMLprim value init_module_arm_android(LLVMModuleRef mod) {
        LLVMContextRef ctx = LLVMGetModuleContext(mod);
        extern unsigned char builtins_bitcode_arm_android[];
        extern int builtins_bitcode_arm_android_length;
        AddBitcodeToModule(builtins_bitcode_arm_android, builtins_bitcode_arm_android_length, llvm::unwrap(ctx), llvm::unwrap(mod));
        return Val_unit;                
    }
    
}
