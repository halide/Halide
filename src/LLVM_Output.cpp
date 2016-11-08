#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "LLVM_Runtime_Linker.h"
#include "CodeGen_LLVM.h"
#include "CodeGen_C.h"
#include "CodeGen_Internal.h"

#include <iostream>
#include <fstream>

namespace Halide {

std::unique_ptr<llvm::raw_fd_ostream> make_raw_fd_ostream(const std::string &filename) {
    std::string error_string;
    std::error_code err;
    std::unique_ptr<llvm::raw_fd_ostream> raw_out(new llvm::raw_fd_ostream(filename, err, llvm::sys::fs::F_None));
    if (err) error_string = err.message();
    internal_assert(error_string.empty())
        << "Error opening output " << filename << ": " << error_string << "\n";

    return raw_out;
}

void emit_file(llvm::Module &module, Internal::LLVMOStream& out, llvm::TargetMachine::CodeGenFileType file_type) {
    Internal::debug(1) << "emit_file.Compiling to native code...\n";
    Internal::debug(2) << "Target triple: " << module.getTargetTriple() << "\n";

    // Get the target specific parser.
    auto target_machine = Internal::make_target_machine(module);
    internal_assert(target_machine.get()) << "Could not allocate target machine!\n";

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

    // Build up all of the passes that we want to do to the module.
    llvm::legacy::PassManager pass_manager;

    pass_manager.add(new llvm::TargetLibraryInfoWrapperPass(llvm::Triple(module.getTargetTriple())));

    // Make sure things marked as always-inline get inlined
    #if LLVM_VERSION < 40
    pass_manager.add(llvm::createAlwaysInlinerPass());
    #else
    pass_manager.add(llvm::createAlwaysInlinerLegacyPass());
    #endif

    // Enable symbol rewriting. This allows code outside libHalide to
    // use symbol rewriting when compiling Halide code (for example, by
    // using cl::ParseCommandLineOption and then passing the appropriate
    // rewrite options via -mllvm flags).
    pass_manager.add(llvm::createRewriteSymbolsPass());

    // Override default to generate verbose assembly.
    target_machine->Options.MCOptions.AsmVerbose = true;

    // Ask the target to add backend passes as necessary.
    target_machine->addPassesToEmitFile(pass_manager, out, file_type);

    pass_manager.run(module);
}

std::unique_ptr<llvm::Module> compile_module_to_llvm_module(const Module &module, llvm::LLVMContext &context) {
    return codegen_llvm(module, context);
}

void compile_llvm_module_to_object(llvm::Module &module, Internal::LLVMOStream& out) {
    emit_file(module, out, llvm::TargetMachine::CGFT_ObjectFile);
}

void compile_llvm_module_to_assembly(llvm::Module &module, Internal::LLVMOStream& out) {
    emit_file(module, out, llvm::TargetMachine::CGFT_AssemblyFile);
}

void compile_llvm_module_to_llvm_bitcode(llvm::Module &module, Internal::LLVMOStream& out) {
    WriteBitcodeToFile(&module, out);
}

void compile_llvm_module_to_llvm_assembly(llvm::Module &module, Internal::LLVMOStream& out) {
    module.print(out, nullptr);
}

void create_static_library(const std::vector<std::string> &src_files, const Target &target,
                    const std::string &dst_file, bool deterministic) {
    internal_assert(!src_files.empty());
#if LLVM_VERSION >= 39
    std::vector<llvm::NewArchiveMember> new_members;
    for (auto &src : src_files) {
        llvm::Expected<llvm::NewArchiveMember> new_member =
            llvm::NewArchiveMember::getFile(src, /*Deterministic=*/true);
        if (!new_member) {
            // Don't use internal_assert: the call to new_member.takeError() will be evaluated
            // even if the assert does not fail, leaving new_member in an indeterminate
            // state.
            internal_error << src << ": " << llvm::toString(new_member.takeError()) << "\n";
        }
        new_members.push_back(std::move(*new_member));
    }
#elif LLVM_VERSION == 38
    std::vector<llvm::NewArchiveIterator> new_members;
    for (auto &src : src_files) {
        new_members.push_back(llvm::NewArchiveIterator(src));
    }
#else
    std::vector<llvm::NewArchiveIterator> new_members;
    for (auto &src : src_files) {
        new_members.push_back(llvm::NewArchiveIterator(src, src));
    }
#endif

    const bool write_symtab = true;
    const auto kind = Internal::get_triple_for_target(target).isOSDarwin()
        ? llvm::object::Archive::K_BSD
        : llvm::object::Archive::K_GNU;
#if LLVM_VERSION == 37
    auto result = llvm::writeArchive(dst_file, new_members,
                       write_symtab, kind,
                       deterministic);
#elif LLVM_VERSION == 38
    const bool thin = false;
    auto result = llvm::writeArchive(dst_file, new_members,
                       write_symtab, kind,
                       deterministic, thin);
#else
    const bool thin = false;
    auto result = llvm::writeArchive(dst_file, new_members,
                       write_symtab, kind,
                       deterministic, thin, nullptr);
#endif
    internal_assert(!result.second) << "Failed to write archive: " << dst_file
        << ", reason: " << result.second << "\n";
}

}  // namespace Halide
