#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>

using namespace Halide;

void testCompileToOutput(Func j) {
    std::string fn_object = Internal::get_test_tmp_dir() + "compile_to_native.o";
    printf("fn_object is %s\n", fn_object.c_str());

    Internal::ensure_no_file_exists(fn_object);

    std::vector<Argument> empty_args;
    j.compile_to({{OutputFileType::object, fn_object}}, empty_args, "");

    Internal::assert_file_exists(fn_object);
}

void testCompileToOutputAndAssembly(Func j) {
    std::string fn_object = Internal::get_test_tmp_dir() + "compile_to_native1.o";
    std::string fn_assembly = Internal::get_test_tmp_dir() + "compile_to_assembly1.s";

    Internal::ensure_no_file_exists(fn_object);
    Internal::ensure_no_file_exists(fn_assembly);

    std::vector<Argument> empty_args;
    j.compile_to({{OutputFileType::object, fn_object}, {OutputFileType::assembly, fn_assembly}}, empty_args, "");

    Internal::assert_file_exists(fn_object);
    Internal::assert_file_exists(fn_assembly);
}

#if HALIDE_WITH_EXCEPTIONS
void testCoffArchiveOutputError(Func j) {
    // This tests the output format, including on non-Windows hosts.
    const Target target("x86-64-windows-no_runtime");
    if (!target.supported()) {
        return;
    }
    namespace fs = std::filesystem;
    const fs::path archive = Internal::get_test_tmp_dir() + "compile_to_blocked.lib";
    fs::create_directory(archive);
    { std::ofstream marker(archive / "keep"); }

    const std::map<OutputFileType, std::string> outputs = {
        {OutputFileType::static_library, archive.string()}};
    bool failed = false;
    try {
        j.compile_to(outputs, j.infer_arguments(), "coff_output", target);
    } catch (const CompileError &e) {
        failed = true;
        internal_assert(std::string(e.what()).find(archive.string()) != std::string::npos);
    }
    internal_assert(failed) << "An unwritable archive destination must fail compilation.\n";
    internal_assert(fs::exists(archive / "keep"));

    fs::remove_all(archive);
    j.compile_to(outputs, j.infer_arguments(), "coff_output", target);
    internal_assert(fs::is_regular_file(archive) && fs::file_size(archive) > 0);

#ifdef _WIN32
    {
        // Allow opening/truncating the file, but block writing its first byte.
        // Closing the handle releases the byte-range lock, even on exceptions.
        std::unique_ptr<void, decltype(&CloseHandle)> handle(
            CreateFileW(archive.c_str(), GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr),
            &CloseHandle);
        internal_assert(handle.get() != INVALID_HANDLE_VALUE);
        OVERLAPPED overlapped{};
        internal_assert(LockFileEx(handle.get(), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                                   0, 1, 0, &overlapped));
        failed = false;
        try {
            j.compile_to(outputs, j.infer_arguments(), "coff_output", target);
        } catch (const CompileError &e) {
            failed = true;
            internal_assert(std::string(e.what()).find(archive.string()) != std::string::npos);
        }
        internal_assert(failed) << "An archive write failure after opening must fail compilation.\n";
        internal_assert(fs::file_size(archive) == 0) << "The output was not opened and truncated.\n";
    }
    j.compile_to(outputs, j.infer_arguments(), "coff_output", target);
    internal_assert(fs::file_size(archive) > 0);
#endif
    fs::remove(archive);
}
#endif

int main(int argc, char **argv) {
    Func f, g, h, j;
    Var x, y;
    f(x, y) = x + y;
    g(x, y) = cast<float>(f(x, y) + f(x + 1, y));
    h(x, y) = f(x, y) + g(x, y);
    j(x, y) = h(x, y) * 2;

    f.compute_root();
    g.compute_root();
    h.compute_root();

    testCompileToOutput(j);

    testCompileToOutputAndAssembly(j);

#if HALIDE_WITH_EXCEPTIONS
    if (Halide::exceptions_enabled()) {
        testCoffArchiveOutputError(j);
    }
#endif

    printf("Success!\n");
    return 0;
}
