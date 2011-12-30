extern "C" {
#include <stdlib.h>

#include <caml/mlvalues.h>
#include <caml/alloc.h>
#include <caml/custom.h>
#include <caml/memory.h>
#include <caml/fail.h>
#include <caml/callback.h>
}

#include <iostream>
#include <sstream>

#include <llvm-c/Core.h>

#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Type.h>
#include <llvm/PassManager.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Instructions.h>
#include <llvm/Intrinsics.h>
#include <llvm/Linker.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/ADT/Triple.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Bitcode/ReaderWriter.h>

#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

extern "C" {

// TODO: factor this into a C++ core function, and a thin OCaml C wrapper
CAMLprim value compile_module_to_string(LLVMModuleRef modref) {
#ifndef __arm__
    Module &mod = *llvm::unwrap(modref);
    LLVMContext &ctx = mod.getContext();
    
    // TODO: streamline this - don't initialize anything but PTX
    LLVMInitializePTXTargetInfo();
    LLVMInitializePTXTarget();
    LLVMInitializePTXTargetMC();
    LLVMInitializePTXAsmPrinter();

    // DISABLED - hooked in here to force PrintBeforeAll option - seems to be the only way?
    /*char* argv[] = { "llc", "-print-before-all" };*/
    /*int argc = sizeof(argv)/sizeof(char*);*/
    /*cl::ParseCommandLineOptions(argc, argv, "Halide PTX internal compiler\n");*/

    // Set up TargetTriple
    mod.setTargetTriple(Triple::normalize("ptx64--"));
    Triple TheTriple(mod.getTargetTriple());
    
    // Allocate target machine
    const std::string MArch = "ptx64";
    const std::string MCPU = "sm_11";
    const Target* TheTarget = 0;
    
    std::string errStr;
    TheTarget = TargetRegistry::lookupTarget(TheTriple.getTriple(), errStr);
    assert(TheTarget);

    TargetOptions Options;
    Options.LessPreciseFPMADOption = true;
    Options.PrintMachineCode = false;
    Options.NoFramePointerElim = false;
    Options.NoFramePointerElimNonLeaf = false;
    Options.NoExcessFPPrecision = false;
    Options.UnsafeFPMath = true;
    Options.NoInfsFPMath = false;
    Options.NoNaNsFPMath = false;
    Options.HonorSignDependentRoundingFPMathOption = false;
    Options.UseSoftFloat = false;
    /* if (FloatABIForCalls != FloatABI::Default) */
        /* Options.FloatABIType = FloatABIForCalls; */
    Options.NoZerosInBSS = false;
    Options.JITExceptionHandling = false;
    Options.JITEmitDebugInfo = false;
    Options.JITEmitDebugInfoToDisk = false;
    Options.GuaranteedTailCallOpt = false;
    Options.StackAlignmentOverride = 0;
    Options.RealignStack = true;
    Options.DisableJumpTables = false;
    Options.TrapFuncName = "";
    Options.EnableSegmentedStacks = false;

    CodeGenOpt::Level OLvl = CodeGenOpt::Default;

    const std::string FeaturesStr = "";
    std::auto_ptr<TargetMachine>
        target(TheTarget->createTargetMachine(TheTriple.getTriple(),
                                              MCPU, FeaturesStr, Options,
                                              llvm::Reloc::Default,
                                              llvm::CodeModel::Default,
                                              OLvl));
    assert(target.get() && "Could not allocate target machine!");
    TargetMachine &Target = *target.get();

    // Set up passes
    PassManager PM;
    // Add the target data from the target machine
    PM.add(new TargetData(*Target.getTargetData()));

    // Override default to generate verbose assembly.
    Target.setAsmVerbosityDefault(true);

    // Output string stream
    std::string outstr;
    raw_string_ostream outs(outstr);
    formatted_raw_ostream ostream(outs);

    // Ask the target to add backend passes as necessary.
    bool fail = Target.addPassesToEmitFile(PM, ostream,
                                           TargetMachine::CGFT_AssemblyFile,
                                           true);
    assert(!fail);

    PM.run(mod);

    ostream.flush();
    std::string& out = outs.str();
    return copy_string(out.c_str());
#else
    return caml_copy_string("NOT IMPLEMENTED ON ARM");
#endif //disable on ARM
}

}
