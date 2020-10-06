#include "LLVM_Output.h"
#include "CodeGen_C.h"
#include "CodeGen_Internal.h"
#include "CodeGen_LLVM.h"
#include "CompilerLogger.h"
#include "LLVM_Headers.h"
#include "LLVM_Runtime_Linker.h"

#include <fstream>
#include <iostream>

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

namespace Internal {
namespace Archive {

// This is a bare-bones Windows .lib file writer, based on inspection
// of the LLVM ArchiveWriter class and the documentation at
// https://www.microsoft.com/msj/0498/hood0498.aspx and
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms680547(v=vs.85).aspx#archive__library__file_format
//
// It has been compared with the output of VS2015's lib.exe and appears to be
// bit-identical (to meaningful bits, anyway) for a sampling of Halide
// AOT output, but it is quite possible that there are omissions, mistakes,
// or just plain bugs.

// Emit a field that is 'size' characters wide.
// If data too small, pad on the right with spaces.
// If data too large, assert.
// Return the offset at which 'data' was written.
template<typename T>
size_t emit_padded(std::ostream &out, T data, size_t size) {
    size_t pos = out.tellp();
    out << data;
    size_t written = (size_t)out.tellp() - pos;
    internal_assert(written <= size);
    while (written < size) {
        out.put(' ');
        written++;
    }
    return pos;
}

using EmitU32 = std::function<void(std::ostream &, uint32_t)>;

void emit_big_endian_u32(std::ostream &out, uint32_t value) {
    out << static_cast<uint8_t>((value >> 24) & 0xff)
        << static_cast<uint8_t>((value >> 16) & 0xff)
        << static_cast<uint8_t>((value >> 8) & 0xff)
        << static_cast<uint8_t>((value)&0xff);
}

void emit_little_endian_u32(std::ostream &out, uint32_t value) {
    out << static_cast<uint8_t>((value)&0xff)
        << static_cast<uint8_t>((value >> 8) & 0xff)
        << static_cast<uint8_t>((value >> 16) & 0xff)
        << static_cast<uint8_t>((value >> 24) & 0xff);
}

void emit_little_endian_u16(std::ostream &out, uint16_t value) {
    out << static_cast<uint8_t>((value)&0xff)
        << static_cast<uint8_t>((value >> 8) & 0xff);
}

// Return the offset at which 'size' was written
size_t finish_member_header(std::ostream &out, size_t size) {
    // Emit zero for all of these, to mimic the 'deterministic' flag
    emit_padded(out, 0, 12);                        // timestamp
    emit_padded(out, ' ', 6);                       // UID
    emit_padded(out, ' ', 6);                       // GID
    emit_padded(out, 0, 8);                         // perm
    const size_t pos = emit_padded(out, size, 10);  // total size of the archive member (not including header)
    out << "\x60\x0A";
    return pos;
}

std::string member_name(const llvm::NewArchiveMember &m) {
    return m.MemberName.str();
}

std::map<std::string, size_t> write_string_table(std::ostream &out,
                                                 const std::vector<llvm::NewArchiveMember> &members) {
    std::map<std::string, size_t> string_to_offset_map;
    size_t start_offset = 0;
    for (const llvm::NewArchiveMember &m : members) {
        std::string name = member_name(m);
        internal_assert(string_to_offset_map.count(name) == 0);
        if (name.size() < 16 && name.find('/') == std::string::npos) {
            // small strings that don't contain '/' can be inlined
            continue;
        }
        if (start_offset == 0) {
            emit_padded(out, "//", 16);
            finish_member_header(out, 0);
            start_offset = out.tellp();
        }
        string_to_offset_map[name] = (size_t)out.tellp() - start_offset;
        out << name;
        out.put('\0');
    }
    // If all strings are short enough, we skip the string table entirely
    if (start_offset != 0) {
        size_t member_end = out.tellp();
        if (out.tellp() % 2) {
            out.put('\x0A');
        }
        size_t final_offset = out.tellp();
        out.seekp(start_offset - 12);
        emit_padded(out, member_end - start_offset, 10);
        out.seekp(final_offset);
    }
    return string_to_offset_map;
}

struct PatchInfo {
    EmitU32 emit_u32;
    size_t pos;
};

void write_symbol_table(std::ostream &out,
                        const std::vector<llvm::NewArchiveMember> &members,
                        bool windows_coff_format,
                        std::map<size_t, std::vector<PatchInfo>> *patchers) {
    internal_assert(!members.empty());

    EmitU32 emit_u32 = windows_coff_format ? emit_little_endian_u32 : emit_big_endian_u32;

    // Write zero for sizes/offsets that will be patched later.
    const size_t kPatchLater = 0;

    std::map<std::string, size_t> name_to_member_index;

    const auto kFileMagicUnknown = llvm::file_magic::unknown;

    llvm::LLVMContext context;
    for (size_t i = 0, n = members.size(); i < n; ++i) {
        llvm::MemoryBufferRef member_buffer = members[i].Buf->getMemBufferRef();
        llvm::Expected<std::unique_ptr<llvm::object::SymbolicFile>> obj_or_err =
            llvm::object::SymbolicFile::createSymbolicFile(
                member_buffer, kFileMagicUnknown, &context);
        if (!obj_or_err) {
            // Don't use internal_assert: the call to new_member.takeError() will be
            // evaluated even if the assert does not fail, leaving new_member in an
            // indeterminate state.
            internal_error << llvm::toString(obj_or_err.takeError()) << "\n";
        }
        llvm::object::SymbolicFile &obj = *obj_or_err.get();
        for (const auto &sym : obj.symbols()) {
#if LLVM_VERSION >= 110
            auto flags = sym.getFlags();
            if (!flags) {
                internal_error << llvm::toString(flags.takeError()) << "\n";
            }
            const uint32_t sym_flags = flags.get();
#else
            const uint32_t sym_flags = sym.getFlags();
#endif
            if (sym_flags & llvm::object::SymbolRef::SF_FormatSpecific) {
                continue;
            }
            if (!(sym_flags & llvm::object::SymbolRef::SF_Global)) {
                continue;
            }
            if ((sym_flags & llvm::object::SymbolRef::SF_Undefined) &&
                !(sym_flags & llvm::object::SymbolRef::SF_Indirect)) {
                continue;
            }
            // Windows COFF doesn't support weak symbols.
            if (sym_flags & llvm::object::SymbolRef::SF_Weak) {
                continue;
            }

            llvm::SmallString<128> symbols_buf;
            llvm::raw_svector_ostream symbols(symbols_buf);
            auto err = sym.printName(symbols);
            internal_assert(!err);
            std::string name = symbols.str().str();
            if (name_to_member_index.find(name) != name_to_member_index.end()) {
                user_warning << "Warning: symbol '" << name << "' seen multiple times in library.\n";
                continue;
            }
            name_to_member_index[name] = i;
        }
    }

    size_t header_start_offset = emit_padded(out, "/", 16);
    size_t symbol_table_size_offset = finish_member_header(out, kPatchLater);  // size of symbol table

    size_t symbol_count_offset = 0;
    if (windows_coff_format) {
        emit_u32(out, members.size());
        for (size_t i = 0, n = members.size(); i < n; ++i) {
            size_t pos = out.tellp();
            emit_u32(out, kPatchLater);  // offset to this .obj member
            (*patchers)[i].push_back({emit_u32, pos});
        }
        symbol_count_offset = out.tellp();
        emit_u32(out, kPatchLater);  // number of symbols
        // symbol-to-archive-member-index, but 1-based rather than zero-based.
        for (auto &it : name_to_member_index) {
            internal_assert(it.second <= 65534);
            emit_little_endian_u16(out, (uint16_t)it.second + 1);
        }
    } else {
        symbol_count_offset = out.tellp();
        emit_u32(out, kPatchLater);  // number of symbols
        for (auto &it : name_to_member_index) {
            size_t pos = out.tellp();
            emit_u32(out, kPatchLater);  // offset to the .obj member containing this symbol
            (*patchers)[it.second].push_back({emit_u32, pos});
        }
    }

    // Symbol table goes at the end for both variants.
    for (auto &it : name_to_member_index) {
        out << it.first;
        out.put('\0');
    }

    size_t member_end = out.tellp();

    // lib.exe pads to 2-byte align with 0x0a
    if (out.tellp() % 2) {
        out.put('\x0A');
    }
    size_t final_offset = out.tellp();

    // Patch the size of the symbol table.
    const size_t member_header_size = 60;
    out.seekp(symbol_table_size_offset);
    emit_padded(out, member_end - member_header_size - header_start_offset, 10);

    // Patch the number of symbols.
    out.seekp(symbol_count_offset);
    emit_u32(out, name_to_member_index.size());

    // Seek back to where we left off.
    out.seekp(final_offset);
}

void write_coff_archive(std::ostream &out,
                        const std::vector<llvm::NewArchiveMember> &members) {
    out << "!<arch>\x0A";

    // First member is named "/" and is the traditional symbol table,
    // with big-endian offsets.
    std::map<size_t, std::vector<PatchInfo>> patchers;
    write_symbol_table(out, members, false, &patchers);

    // Second member (for Windows COFF) is also named "/" and is also a symbol table,
    // but with little-endian offsets and with symbols sorted by name. (We actually sort
    // both tables as a side-effect, but the first isn't required to be sorted.)
    write_symbol_table(out, members, true, &patchers);

    // Third member, named "//", is the optional string table. (MS docs say it is required but
    // lib.exe only emits as needed, so we will follow its example)
    std::map<std::string, size_t> string_to_offset_map = write_string_table(out, members);

    // The remaining members are just (header + contents of .obj file).
    std::vector<size_t> member_offset;
    for (const llvm::NewArchiveMember &m : members) {
        size_t pos = out.tellp();
        member_offset.push_back(pos);

        std::string name = member_name(m);
        auto it = string_to_offset_map.find(name);
        if (it != string_to_offset_map.end()) {
            out.put('/');
            emit_padded(out, it->second, 15);
        } else {
            emit_padded(out, name + "/", 16);
        }
        size_t size = m.Buf->getBufferSize();
        finish_member_header(out, size);

        out << m.Buf->getMemBufferRef().getBuffer().str();

        if (out.tellp() % 2) {
            out.put('\x0A');
        }
    }

    for (auto &it : patchers) {
        size_t i = it.first;
        for (auto &patcher : it.second) {
            out.seekp(patcher.pos);
            patcher.emit_u32(out, member_offset.at(i));
        }
    }
}

}  // namespace Archive
}  // namespace Internal

std::unique_ptr<llvm::raw_fd_ostream> make_raw_fd_ostream(const std::string &filename) {
    std::string error_string;
    std::error_code err;
    std::unique_ptr<llvm::raw_fd_ostream> raw_out(new llvm::raw_fd_ostream(filename, err, llvm::sys::fs::F_None));
    if (err) {
        error_string = err.message();
    }
    internal_assert(error_string.empty())
        << "Error opening output " << filename << ": " << error_string << "\n";

    return raw_out;
}

namespace {

// llvm::CloneModule has issues with debug info. As a workaround,
// serialize it to bitcode in memory, and then parse the bitcode back in.
std::unique_ptr<llvm::Module> clone_module(const llvm::Module &module_in) {
    Internal::debug(2) << "Cloning module " << module_in.getName().str() << "\n";

    // Write the module to a buffer.
    llvm::SmallVector<char, 16> clone_buffer;
    llvm::raw_svector_ostream clone_ostream(clone_buffer);
    WriteBitcodeToFile(module_in, clone_ostream);

    // Read it back in.
    llvm::MemoryBufferRef buffer_ref(llvm::StringRef(clone_buffer.data(), clone_buffer.size()), "clone_buffer");
    auto cloned_module = llvm::parseBitcodeFile(buffer_ref, module_in.getContext());
    internal_assert(cloned_module);

    return std::move(cloned_module.get());
}

}  // namespace

void emit_file(const llvm::Module &module_in, Internal::LLVMOStream &out,
#if LLVM_VERSION >= 100
               llvm::CodeGenFileType file_type
#else
               llvm::TargetMachine::CodeGenFileType file_type
#endif
) {
    Internal::debug(1) << "emit_file.Compiling to native code...\n";
    Internal::debug(2) << "Target triple: " << module_in.getTargetTriple() << "\n";

    auto time_start = std::chrono::high_resolution_clock::now();

    // Work on a copy of the module to avoid modifying the original.
    std::unique_ptr<llvm::Module> module = clone_module(module_in);

    // Get the target specific parser.
    auto target_machine = Internal::make_target_machine(*module);
    internal_assert(target_machine.get()) << "Could not allocate target machine!\n";

    llvm::DataLayout target_data_layout(target_machine->createDataLayout());
    if (!(target_data_layout == module->getDataLayout())) {
        internal_error << "Warning: module's data layout does not match target machine's\n"
                       << target_data_layout.getStringRepresentation() << "\n"
                       << module->getDataLayout().getStringRepresentation() << "\n";
    }

    // Build up all of the passes that we want to do to the module.
    llvm::legacy::PassManager pass_manager;

    pass_manager.add(new llvm::TargetLibraryInfoWrapperPass(llvm::Triple(module->getTargetTriple())));

    // Make sure things marked as always-inline get inlined
    pass_manager.add(llvm::createAlwaysInlinerLegacyPass());

    // Remove any stale debug info
    pass_manager.add(llvm::createStripDeadDebugInfoPass());

    // Enable symbol rewriting. This allows code outside libHalide to
    // use symbol rewriting when compiling Halide code (for example, by
    // using cl::ParseCommandLineOption and then passing the appropriate
    // rewrite options via -mllvm flags).
    pass_manager.add(llvm::createRewriteSymbolsPass());

    // Override default to generate verbose assembly.
    target_machine->Options.MCOptions.AsmVerbose = true;

    // Ask the target to add backend passes as necessary.
    target_machine->addPassesToEmitFile(pass_manager, out, nullptr, file_type);

    pass_manager.run(*module);

    auto *logger = Internal::get_compiler_logger();
    if (logger) {
        auto time_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = time_end - time_start;
        logger->record_compilation_time(Internal::CompilerLogger::Phase::LLVM, diff.count());
    }

    // If -time-passes is in HL_LLVM_ARGS, this will print llvm passes time statstics otherwise its no-op.
    llvm::reportAndResetTimings();
}

std::unique_ptr<llvm::Module> compile_module_to_llvm_module(const Module &module, llvm::LLVMContext &context) {
    return codegen_llvm(module, context);
}

void compile_llvm_module_to_object(llvm::Module &module, Internal::LLVMOStream &out) {
#if LLVM_VERSION >= 100
    emit_file(module, out, llvm::CGFT_ObjectFile);
#else
    emit_file(module, out, llvm::TargetMachine::CGFT_ObjectFile);
#endif
}

void compile_llvm_module_to_assembly(llvm::Module &module, Internal::LLVMOStream &out) {
#if LLVM_VERSION >= 100
    emit_file(module, out, llvm::CGFT_AssemblyFile);
#else
    emit_file(module, out, llvm::TargetMachine::CGFT_AssemblyFile);
#endif
}

void compile_llvm_module_to_llvm_bitcode(llvm::Module &module, Internal::LLVMOStream &out) {
    WriteBitcodeToFile(module, out);
}

void compile_llvm_module_to_llvm_assembly(llvm::Module &module, Internal::LLVMOStream &out) {
    module.print(out, nullptr);
}

// Note that the utilities for get/set working directory are deliberately *not* in Util.h;
// generally speaking, you shouldn't ever need or want to do this, and doing so is asking for
// trouble. This exists solely to work around an issue with LLVM, hence its restricted
// location. If we ever legitimately need this elsewhere, consider moving it to Util.h.
namespace {

std::string get_current_directory() {
#ifdef _WIN32
    DWORD dir_buf_size = GetCurrentDirectoryW(0, nullptr);
    internal_assert(dir_buf_size) << "GetCurrentDirectoryW() failed; error " << GetLastError() << "\n";

    // GetCurrentDirectoryW returns a _buffer size_, not a character count.
    // std::wstring null-terminates on its own, so don't count that here.
    std::wstring wdir(dir_buf_size - 1, 0);

    DWORD ret = GetCurrentDirectoryW(dir_buf_size, &wdir[0]);
    internal_assert(ret) << "GetCurrentDirectoryW() failed; error " << GetLastError() << "\n";

    int dir_len = WideCharToMultiByte(CP_UTF8, 0, &wdir[0], (int)wdir.size(), nullptr, 0, nullptr, nullptr);
    internal_assert(dir_len) << "WideCharToMultiByte() failed; error " << GetLastError() << "\n";

    std::string dir(dir_len, 0);

    ret = WideCharToMultiByte(CP_UTF8, 0, &wdir[0], (int)wdir.size(), &dir[0], (int)dir.size(), nullptr, nullptr);
    internal_assert(ret) << "WideCharToMultiByte() failed; error " << GetLastError() << "\n";

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
    int n_chars = MultiByteToWideChar(CP_UTF8, 0, &d[0], (int)d.size(), nullptr, 0);
    internal_assert(n_chars) << "MultiByteToWideChar() failed; error " << GetLastError() << "\n";

    std::wstring wd(n_chars, 0);
    int ret = MultiByteToWideChar(CP_UTF8, 0, &d[0], (int)d.size(), &wd[0], wd.size());
    internal_assert(ret) << "MultiByteToWideChar() failed; error " << GetLastError() << "\n";

    internal_assert(SetCurrentDirectoryW(wd.c_str())) << "SetCurrentDirectoryW() failed; error " << GetLastError() << "\n";
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
    return {dir, file};
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
    explicit SetCwd(const std::string &d)
        : original_directory(get_current_directory()) {
        if (!d.empty()) {
            set_current_directory(d);
        }
    }
    ~SetCwd() {
        set_current_directory(original_directory);
    }
};

}  // namespace

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

    // LLVM can't write MS PE/COFF Lib format, which is almost-but-not-quite
    // the same as GNU ar format.
    if (Internal::get_triple_for_target(target).isWindowsMSVCEnvironment()) {
        std::ofstream f(dst_file, std::ios_base::trunc | std::ios_base::binary);
        Internal::Archive::write_coff_archive(f, new_members);
        f.flush();
        f.close();
        return;
    }

    const bool write_symtab = true;
    const auto kind = Internal::get_triple_for_target(target).isOSDarwin() ? llvm::object::Archive::K_BSD : llvm::object::Archive::K_GNU;
    const bool thin = false;
    auto result = llvm::writeArchive(dst_file, new_members,
                                     write_symtab, kind,
                                     deterministic, thin, nullptr);
    internal_assert(!result)
        << "Failed to write archive: " << dst_file
        << ", reason: " << llvm::toString(std::move(result)) << "\n";
}

}  // namespace Halide
