#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "CodeGen_LLVM.h"
#include "CodeGen_C.h"

#include <iostream>
#include <fstream>

namespace Halide {

llvm::raw_fd_ostream *new_raw_fd_ostream(const std::string &filename) {
    std::string error_string;
    #if LLVM_VERSION < 35
    llvm::raw_fd_ostream *raw_out = new llvm::raw_fd_ostream(filename.c_str(), error_string);
    #elif LLVM_VERSION == 35
    llvm::raw_fd_ostream *raw_out = new llvm::raw_fd_ostream(filename.c_str(), error_string, llvm::sys::fs::F_None);
    #else // llvm 3.6
    std::error_code err;
    llvm::raw_fd_ostream *raw_out = new llvm::raw_fd_ostream(filename.c_str(), err, llvm::sys::fs::F_None);
    if (err) error_string = err.message();
    #endif
    internal_assert(error_string.empty())
        << "Error opening output " << filename << ": " << error_string << "\n";

    return raw_out;
}

namespace Internal {

bool get_md_bool(LLVMMDNodeArgumentType value, bool &result) {
    #if LLVM_VERSION < 36 || defined(WITH_NATIVE_CLIENT)
    llvm::ConstantInt *c = llvm::cast<llvm::ConstantInt>(value);
    #else
    llvm::ConstantAsMetadata *cam = llvm::cast<llvm::ConstantAsMetadata>(value);
    llvm::ConstantInt *c = llvm::cast<llvm::ConstantInt>(cam->getValue());
    #endif
    if (c) {
        result = !c->isZero();
        return true;
    }
    return false;
}

bool get_md_string(LLVMMDNodeArgumentType value, std::string &result) {
    #if LLVM_VERSION < 36
    if (llvm::dyn_cast<llvm::ConstantAggregateZero>(value)) {
        result = "";
        return true;
    }
    llvm::ConstantDataArray *c = llvm::cast<llvm::ConstantDataArray>(value);
    if (c) {
        result = c->getAsCString();
        return true;
    }
    #else
    llvm::MDString *c = llvm::dyn_cast<llvm::MDString>(value);
    if (c) {
        result = c->getString();
        return true;
    }
    #endif
    return false;
}

}

void get_target_options(const llvm::Module *module, llvm::TargetOptions &options, std::string &mcpu, std::string &mattrs) {
    bool use_soft_float_abi = false;
    Internal::get_md_bool(module->getModuleFlag("halide_use_soft_float_abi"), use_soft_float_abi);
    Internal::get_md_string(module->getModuleFlag("halide_mcpu"), mcpu);
    Internal::get_md_string(module->getModuleFlag("halide_mattrs"), mattrs);

    options = llvm::TargetOptions();
    options.LessPreciseFPMADOption = true;
    options.NoFramePointerElim = false;
    options.AllowFPOpFusion = llvm::FPOpFusion::Fast;
    options.UnsafeFPMath = true;
    options.NoInfsFPMath = true;
    options.NoNaNsFPMath = true;
    options.HonorSignDependentRoundingFPMathOption = false;
    options.UseSoftFloat = false;
    options.NoZerosInBSS = false;
    options.GuaranteedTailCallOpt = false;
    options.DisableTailCalls = false;
    options.StackAlignmentOverride = 0;
    options.TrapFuncName = "";
    options.PositionIndependentExecutable = true;
    options.FunctionSections = true;
    #ifdef WITH_NATIVE_CLIENT
    options.UseInitArray = true;
    #else
    options.UseInitArray = false;
    #endif
    options.FloatABIType =
        use_soft_float_abi ? llvm::FloatABI::Soft : llvm::FloatABI::Hard;
}


void clone_target_options(const llvm::Module *from, llvm::Module *to) {
    to->setTargetTriple(from->getTargetTriple());

    llvm::LLVMContext &context = to->getContext();

    bool use_soft_float_abi = false;
    if (Internal::get_md_bool(from->getModuleFlag("halide_use_soft_float_abi"), use_soft_float_abi))
        to->addModuleFlag(llvm::Module::Warning, "halide_use_soft_float_abi", use_soft_float_abi ? 1 : 0);

    std::string mcpu;
    if (Internal::get_md_string(from->getModuleFlag("halide_mcpu"), mcpu)) {
        #if LLVM_VERSION < 36
        to->addModuleFlag(llvm::Module::Warning, "halide_mcpu", llvm::ConstantDataArray::getString(context, mcpu));
        #else
        to->addModuleFlag(llvm::Module::Warning, "halide_mcpu", llvm::MDString::get(context, mcpu));
        #endif
    }

    std::string mattrs;
    if (Internal::get_md_string(from->getModuleFlag("halide_mattrs"), mattrs)) {
        #if LLVM_VERSION < 36
        to->addModuleFlag(llvm::Module::Warning, "halide_mattrs", llvm::ConstantDataArray::getString(context, mattrs));
        #else
        to->addModuleFlag(llvm::Module::Warning, "halide_mattrs", llvm::MDString::get(context, mattrs));
        #endif
    }
}


llvm::TargetMachine *get_target_machine(const llvm::Module *module) {
    std::string error_string;

    const llvm::Target *target = llvm::TargetRegistry::lookupTarget(module->getTargetTriple(), error_string);
    if (!target) {
        std::cout << error_string << std::endl;
        llvm::TargetRegistry::printRegisteredTargetsForVersion();
    }
    internal_assert(target) << "Could not create target\n";

    llvm::TargetOptions options;
    std::string mcpu = "";
    std::string mattrs = "";
    get_target_options(module, options, mcpu, mattrs);

    return target->createTargetMachine(module->getTargetTriple(),
                                       mcpu, mattrs,
                                       options,
                                       llvm::Reloc::PIC_,
                                       llvm::CodeModel::Default,
                                       llvm::CodeGenOpt::Aggressive);
}

void emit_file(llvm::Module *module, const std::string &filename, llvm::TargetMachine::CodeGenFileType file_type) {
    Internal::debug(1) << "Compiling to native code...\n";
    Internal::debug(2) << "Target triple: " << module->getTargetTriple() << "\n";

    // Get the target specific parser.
    llvm::TargetMachine *target_machine = get_target_machine(module);
    internal_assert(target_machine) << "Could not allocate target machine!\n";

    // Build up all of the passes that we want to do to the module.
    #if LLVM_VERSION < 37
    llvm::PassManager pass_manager;
    #else
    llvm::legacy::PassManager pass_manager;
    #endif

    #if LLVM_VERSION < 37
    // Add an appropriate TargetLibraryInfo pass for the module's triple.
    pass_manager.add(new llvm::TargetLibraryInfo(llvm::Triple(module->getTargetTriple())));
    #else
    pass_manager.add(new llvm::TargetLibraryInfoWrapperPass(llvm::Triple(module->getTargetTriple())));
    #endif

    #if LLVM_VERSION < 33
    pass_manager.add(new llvm::TargetTransformInfo(target_machine->getScalarTargetTransformInfo(),
                                                   target_machine->getVectorTargetTransformInfo()));
    #elif LLVM_VERSION < 37
    target_machine->addAnalysisPasses(pass_manager);
    #endif

    #if LLVM_VERSION < 35
    llvm::DataLayout *layout = new llvm::DataLayout(module);
    Internal::debug(2) << "Data layout: " << layout->getStringRepresentation();
    pass_manager.add(layout);
    #endif

    // Make sure things marked as always-inline get inlined
    pass_manager.add(llvm::createAlwaysInlinerPass());

    // Override default to generate verbose assembly.
    #if LLVM_VERSION < 37
    target_machine->setAsmVerbosityDefault(true);
    #else
    target_machine->Options.MCOptions.AsmVerbose = true;
    #endif

    llvm::raw_fd_ostream *raw_out = new_raw_fd_ostream(filename);
    llvm::formatted_raw_ostream *out = new llvm::formatted_raw_ostream(*raw_out);

    // Ask the target to add backend passes as necessary.
    target_machine->addPassesToEmitFile(pass_manager, *out, file_type);

    pass_manager.run(*module);

    delete out;
    delete raw_out;

    delete target_machine;
}

llvm::Module *compile_module_to_llvm_module(const Module &module, llvm::LLVMContext &context) {
    return codegen_llvm(module, context);
}

void compile_llvm_module_to_object(llvm::Module *module, const std::string &filename) {
    emit_file(module, filename, llvm::TargetMachine::CGFT_ObjectFile);
}

void compile_llvm_module_to_assembly(llvm::Module *module, const std::string &filename) {
    emit_file(module, filename, llvm::TargetMachine::CGFT_AssemblyFile);
}

void compile_llvm_module_to_native(llvm::Module *module,
                                   const std::string &object_filename,
                                   const std::string &assembly_filename) {
    emit_file(module, object_filename, llvm::TargetMachine::CGFT_ObjectFile);
    emit_file(module, assembly_filename, llvm::TargetMachine::CGFT_AssemblyFile);
}

void compile_llvm_module_to_llvm_bitcode(llvm::Module *module, const std::string &filename) {
    llvm::raw_fd_ostream *file = new_raw_fd_ostream(filename);
    WriteBitcodeToFile(module, *file);
    delete file;
}

void compile_llvm_module_to_llvm_assembly(llvm::Module *module, const std::string &filename) {
    llvm::raw_fd_ostream *file = new_raw_fd_ostream(filename);
    module->print(*file, NULL);
    delete file;
}

}  // namespace Halide
