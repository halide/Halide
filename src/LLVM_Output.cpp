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
    #if LLVM_VERSION < 35
    std::unique_ptr<llvm::raw_fd_ostream> raw_out(new llvm::raw_fd_ostream(filename.c_str(), error_string));
    #elif LLVM_VERSION == 35
    std::unique_ptr<llvm::raw_fd_ostream> raw_out(new llvm::raw_fd_ostream(filename.c_str(), error_string, llvm::sys::fs::F_None));
    #else // llvm 3.6 or later
    std::error_code err;
    std::unique_ptr<llvm::raw_fd_ostream> raw_out(new llvm::raw_fd_ostream(filename, err, llvm::sys::fs::F_None));
    if (err) error_string = err.message();
    #endif
    internal_assert(error_string.empty())
        << "Error opening output " << filename << ": " << error_string << "\n";

    return raw_out;
}


#if LLVM_VERSION < 37
void emit_file_legacy(llvm::Module &module, Internal::LLVMOStream& out, llvm::TargetMachine::CodeGenFileType file_type) {
    Internal::debug(1) << "emit_file_legacy.Compiling to native code...\n";
    Internal::debug(2) << "Target triple: " << module.getTargetTriple() << "\n";

    // Get the target specific parser.
    auto target_machine = Internal::make_target_machine(module);
    internal_assert(target_machine.get()) << "Could not allocate target machine!\n";

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

    std::unique_ptr<llvm::formatted_raw_ostream> formatted_out(new llvm::formatted_raw_ostream(out));

    // Ask the target to add backend passes as necessary.
    target_machine->addPassesToEmitFile(pass_manager, *formatted_out, file_type);

    pass_manager.run(module);
}
#endif

void emit_file(llvm::Module &module, Internal::LLVMOStream& out, llvm::TargetMachine::CodeGenFileType file_type) {
#if LLVM_VERSION < 37
    emit_file_legacy(module, out, file_type);
#else
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

    // Override default to generate verbose assembly.
    target_machine->Options.MCOptions.AsmVerbose = true;

    // Ask the target to add backend passes as necessary.
    target_machine->addPassesToEmitFile(pass_manager, out, file_type);

    pass_manager.run(module);
#endif
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
#if LLVM_VERSION >= 37 && !defined(WITH_NATIVE_CLIENT)

#if LLVM_VERSION >= 39
    std::vector<llvm::NewArchiveMember> new_members;
    for (auto &src : src_files) {
        llvm::Expected<llvm::NewArchiveMember> new_member =
            llvm::NewArchiveMember::getFile(src, /*Deterministic=*/true);
        internal_assert((bool)new_member)
            << src << ": " << llvm::toString(new_member.takeError()) << "\n";
        new_members.push_back(std::move(*new_member));
    }
#elif LLVM_VERSION == 37
    std::vector<llvm::NewArchiveIterator> new_members;
    for (auto &src : src_files) {
        new_members.push_back(llvm::NewArchiveIterator(src, src));
    }
#else
    std::vector<llvm::NewArchiveIterator> new_members;
    for (auto &src : src_files) {
        new_members.push_back(llvm::NewArchiveIterator(src));
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
#else  // <= 3.6 or PNacl
    // LLVM 3.6-and-earlier don't expose the right API. Halide no longer officially
    // supports these versions, but we still do some build-and-testing on them
    // as a way to improve pnacl-llvm coverage (since it's somewhere between 3.6 and 3.7),
    // so as a stopgap measure to allow Makefiles to rely on static_library output,
    // shell out to 'ar' to do the work. This will hopefully be short-lived as even
    // limited support for pre-3.7 shouldn't need to live much longer.
    // (Also note that we don't use the "deterministic" flag since not all ar implementations
    // support it.)
    std::string command = "ar qsv " + dst_file;
    for (auto &src : src_files) {
        command += " " + src;
    }
    int result = system(command.c_str());
    internal_assert(result == 0) << "shelling out to ar failed.\n";
#endif
}

}  // namespace Halide
