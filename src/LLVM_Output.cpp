#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "CodeGen_LLVM.h"
#include "CodeGen_C.h"
#include "CodeGen_Internal.h"

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


#if LLVM_VERSION < 37
void emit_file_legacy(llvm::Module &module, const std::string &filename, llvm::TargetMachine::CodeGenFileType file_type) {
    Internal::debug(1) << "emit_file_legacy.Compiling to native code...\n";
    Internal::debug(2) << "Target triple: " << module.getTargetTriple() << "\n";

    // Get the target specific parser.
    llvm::TargetMachine *target_machine = Internal::get_target_machine(module);
    internal_assert(target_machine) << "Could not allocate target machine!\n";

    // Build up all of the passes that we want to do to the module.
    llvm::PassManager pass_manager;

    // Add an appropriate TargetLibraryInfo pass for the module's triple.
    pass_manager.add(new llvm::TargetLibraryInfo(llvm::Triple(module.getTargetTriple())));

    #if LLVM_VERSION < 33
    pass_manager.add(new llvm::TargetTransformInfo(target_machine->getScalarTargetTransformInfo(),
                                                   target_machine->getVectorTargetTransformInfo()));
    #else
    target_machine->addAnalysisPasses(pass_manager);
    #endif

    #if LLVM_VERSION < 35
    llvm::DataLayout *layout = new llvm::DataLayout(module.get());
    Internal::debug(2) << "Data layout: " << layout->getStringRepresentation();
    pass_manager.add(layout);
    #endif

    // Make sure things marked as always-inline get inlined
    pass_manager.add(llvm::createAlwaysInlinerPass());

    // Override default to generate verbose assembly.
    target_machine->setAsmVerbosityDefault(true);

    llvm::raw_fd_ostream *raw_out = new_raw_fd_ostream(filename);
    llvm::formatted_raw_ostream *out = new llvm::formatted_raw_ostream(*raw_out);

    // Ask the target to add backend passes as necessary.
    target_machine->addPassesToEmitFile(pass_manager, *out, file_type);

    pass_manager.run(module);

    delete out;
    delete raw_out;

    delete target_machine;
}
#endif

void emit_file(llvm::Module &module, const std::string &filename, llvm::TargetMachine::CodeGenFileType file_type) {
#if LLVM_VERSION < 37
    emit_file_legacy(module, filename, file_type);
#else
    Internal::debug(1) << "emit_file.Compiling to native code...\n";
    Internal::debug(2) << "Target triple: " << module.getTargetTriple() << "\n";

    // Get the target specific parser.
    llvm::TargetMachine *target_machine = Internal::get_target_machine(module);
    internal_assert(target_machine) << "Could not allocate target machine!\n";

    #if LLVM_VERSION == 37
        llvm::DataLayout target_data_layout(*(target_machine->getDataLayout()));
    #else
        llvm::DataLayout target_data_layout(target_machine->createDataLayout());
    #endif
    if (!(target_data_layout == module.getDataLayout())) {
        internal_error << "Warning: module's data layout does not match target machine's\n"
                       << target_data_layout.getStringRepresentation() << "\n"
                       << module.getDataLayout().getStringRepresentation() << "\n";
    }

    std::unique_ptr<llvm::raw_fd_ostream> out(new_raw_fd_ostream(filename));

    // Build up all of the passes that we want to do to the module.
    llvm::legacy::PassManager pass_manager;

    pass_manager.add(new llvm::TargetLibraryInfoWrapperPass(llvm::Triple(module.getTargetTriple())));

    // Make sure things marked as always-inline get inlined
    pass_manager.add(llvm::createAlwaysInlinerPass());

    // Override default to generate verbose assembly.
    target_machine->Options.MCOptions.AsmVerbose = true;

    // Ask the target to add backend passes as necessary.
    target_machine->addPassesToEmitFile(pass_manager, *out, file_type);

    pass_manager.run(module);

    delete target_machine;
#endif
}

std::unique_ptr<llvm::Module> compile_module_to_llvm_module(const Module &module, llvm::LLVMContext &context) {
    return codegen_llvm(module, context);
}

void compile_llvm_module_to_object(llvm::Module &module, const std::string &filename) {
    emit_file(module, filename, llvm::TargetMachine::CGFT_ObjectFile);
}

void compile_llvm_module_to_assembly(llvm::Module &module, const std::string &filename) {
    emit_file(module, filename, llvm::TargetMachine::CGFT_AssemblyFile);
}

void compile_llvm_module_to_native(llvm::Module &module,
                                   const std::string &object_filename,
                                   const std::string &assembly_filename) {
    emit_file(module, object_filename, llvm::TargetMachine::CGFT_ObjectFile);
    emit_file(module, assembly_filename, llvm::TargetMachine::CGFT_AssemblyFile);
}

void compile_llvm_module_to_llvm_bitcode(llvm::Module &module, const std::string &filename) {
    llvm::raw_fd_ostream *file = new_raw_fd_ostream(filename);
    WriteBitcodeToFile(&module, *file);
    delete file;
}

void compile_llvm_module_to_llvm_assembly(llvm::Module &module, const std::string &filename) {
    llvm::raw_fd_ostream *file = new_raw_fd_ostream(filename);
    module.print(*file, nullptr);
    delete file;
}

}  // namespace Halide
