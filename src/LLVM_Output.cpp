#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "LLVM_Runtime_Linker.h"
#include "CodeGen_LLVM.h"
#include "CodeGen_C.h"
#include "CodeGen_Internal.h"

#include <iostream>
#include <fstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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

// Note that the utilities for get/set working directory are deliberately *not* in Util.h;
// generally speaking, you shouldn't ever need or want to do this, and doing so is asking for
// trouble. This exists solely to work around an issue with LLVM, hence its restricted
// location. If we ever legitimately need this elsewhere, consider moving it to Util.h.
namespace {

std::string get_current_directory() {
#ifdef _WIN32
    std::string dir;
    char p[MAX_PATH];
    DWORD ret = GetCurrentDirectoryA(MAX_PATH, p);
    internal_assert(ret != 0) << "GetCurrentDirectoryA() failed";
    dir = p;
    return dir;
#else
    std::string dir;
    // Note that passing null for the first arg isn't strictly POSIX, but is
    // supported everywhere we currently build.
    char *p = getcwd(nullptr, 0);
    internal_assert(p != NULL) << "getcwd() failed";
    dir = p;
    free(p);
    return dir;
#endif
}

void set_current_directory(const std::string &d) {
#ifdef _WIN32
    internal_assert(SetCurrentDirectoryA(d.c_str())) << "SetCurrentDirectoryA() failed";
#else
    internal_assert(chdir(d.c_str()) == 0) << "chdir() failed";
#endif
}

std::pair<std::string, std::string> dir_and_file(const std::string &path) {
    std::string dir, file;
    size_t slash_pos = path.rfind('/');
#ifdef _WIN32
    if (slash_pos == std::string::npos) {
        // Windows is a thing
        slash_pos = path.rfind('\\');
    }
#endif
    if (slash_pos != std::string::npos) {
        dir = path.substr(0, slash_pos);
        file = path.substr(slash_pos + 1);
    } else {
        file = path;
    }
    return { dir, file };
}

std::string make_absolute_path(const std::string &path) {
    bool is_absolute = !path.empty() && path[0] == '/';
    char sep = '/';
#ifdef _WIN32
    // Allow for C:\whatever or c:/whatever on Windows
    if (path.size() >= 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
        is_absolute = true;
        sep = path[2];
    } else if (path.size() > 2 && path[0] == '\\' && path[1] == '\\') {
        // Also allow for UNC-style paths beginning with double-backslash
        is_absolute = true;
        sep = path[0];
    }
#endif
    if (!is_absolute) {
        return get_current_directory() + sep + path;
    }
    return path;
}

struct SetCwd {
    const std::string original_directory;
    explicit SetCwd(const std::string &d) : original_directory(get_current_directory()) {
        if (!d.empty()) {
            set_current_directory(d);
        }
    }
    ~SetCwd() {
        set_current_directory(original_directory);
    }
};

}

void create_static_library(const std::vector<std::string> &src_files_in, const Target &target,
                           const std::string &dst_file_in, bool deterministic) {
    internal_assert(!src_files_in.empty());

    // Ensure that dst_file is an absolute path, since we're going to change the
    // working directory temporarily.
    std::string dst_file = make_absolute_path(dst_file_in);

    // If we give absolute paths to LLVM, it will dutifully embed them in the resulting
    // .a file; some versions of 'ar x' are unable to deal with the resulting files,
    // which is inconvenient. So let's doctor the inputs to be simple filenames,
    // and temporarily change the working directory. (Note that this requires all the
    // input files be in the same directory; this is currently always the case for
    // our existing usage.)
    std::string src_dir = dir_and_file(src_files_in.front()).first;
    std::vector<std::string> src_files;
    for (auto &s_in : src_files_in) {
        auto df = dir_and_file(s_in);
        internal_assert(df.first == src_dir) << "All inputs to create_static_library() must be in the same directory";
        for (auto &s_existing : src_files) {
            internal_assert(s_existing != df.second) << "create_static_library() does not allow duplicate filenames.";
        }
        src_files.push_back(df.second);
    }

    SetCwd set_cwd(src_dir);

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
