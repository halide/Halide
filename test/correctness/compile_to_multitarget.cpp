#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;

// Given a path like /path/to/some/file.ext, return file.ext
// If the path contains no separators (/ or \), just return it as-is
std::string leaf_name(const std::string &path) {
    size_t sep = std::min(path.rfind('/'), path.rfind('\\'));
    return path.substr(sep == std::string::npos ? 0 : sep + 1);
}

std::string get_output_path_prefix(const std::string &base) {
    return Internal::get_test_tmp_dir() + "halide_test_correctness_compile_to_multitarget_" + base;
}

void test_compile_to_static_library(Func j) {
    std::string filename_prefix = get_output_path_prefix("c1");
    const char *a = get_host_target().os == Target::Windows ? ".lib" : ".a";

    std::vector<Target> targets = {
        Target("host-profile-no_bounds_query"),
        Target("host-profile"),
    };

    std::vector<std::string> files;
    files.push_back(filename_prefix + ".h");
    files.push_back(filename_prefix + a);

    for (auto f : files) {
        Internal::ensure_no_file_exists(f);
    }

    j.compile_to_multitarget_static_library(filename_prefix, j.infer_arguments(), targets);

    for (auto f : files) {
        Internal::assert_file_exists(f);
    }

    // TODO: would be nice to examine the contents of the library and verify the
    // sub-objects have the filenames we expect.
}

void test_compile_to_object_files(Func j) {
    std::string filename_prefix = get_output_path_prefix("c2");
    const char *o = get_host_target().os == Target::Windows ? ".obj" : ".o";

    std::vector<std::string> target_strings = {
        "host-profile-no_bounds_query",
        "host-profile",
    };

    std::vector<Target> targets;
    for (auto s : target_strings) {
        targets.emplace_back(s);
    }

    std::vector<std::string> files;
    files.push_back(filename_prefix + ".h");
    files.push_back(filename_prefix + "_runtime" + o);
    files.push_back(filename_prefix + "_wrapper" + o);
    for (auto s : target_strings) {
        files.push_back(filename_prefix + "-" + s + o);
    }

    for (auto f : files) {
        Internal::ensure_no_file_exists(f);
    }

    j.compile_to_multitarget_object_files(filename_prefix, j.infer_arguments(), targets, target_strings);

    for (auto f : files) {
        Internal::assert_file_exists(f);
    }
}

void test_compile_to_object_files_no_runtime(Func j) {
    std::string filename_prefix = get_output_path_prefix("c3");
    const char *o = get_host_target().os == Target::Windows ? ".obj" : ".o";

    std::vector<std::string> target_strings = {
        "host-profile-no_bounds_query-no_runtime",
        "host-profile-no_runtime",
    };

    std::vector<Target> targets;
    for (auto s : target_strings) {
        targets.emplace_back(s);
    }

    std::vector<std::string> files;
    files.push_back(filename_prefix + ".h");
    files.push_back(filename_prefix + "_wrapper" + o);
    for (auto s : target_strings) {
        files.push_back(filename_prefix + "-" + s + o);
    }

    for (auto f : files) {
        Internal::ensure_no_file_exists(f);
    }

    j.compile_to_multitarget_object_files(filename_prefix, j.infer_arguments(), targets, target_strings);

    for (auto f : files) {
        Internal::assert_file_exists(f);
    }
}

void test_compile_to_object_files_single_target(Func j) {
    std::string filename_prefix = get_output_path_prefix("c4");
    const char *o = get_host_target().os == Target::Windows ? ".obj" : ".o";

    std::vector<std::string> target_strings = {
        "host-debug",
    };

    std::vector<Target> targets;
    for (auto s : target_strings) {
        targets.emplace_back(s);
    }

    std::vector<std::string> files;
    files.push_back(filename_prefix + ".h");
    files.push_back(filename_prefix + o);

    for (auto f : files) {
        Internal::ensure_no_file_exists(f);
    }

    j.compile_to_multitarget_object_files(filename_prefix, j.infer_arguments(), targets, target_strings);

    for (auto f : files) {
        Internal::assert_file_exists(f);
    }
}

void test_compile_to_everything(Func j, bool do_object) {
    std::string filename_prefix = get_output_path_prefix(do_object ? "c5" : "c6");
    const char *a = get_host_target().os == Target::Windows ? ".lib" : ".a";
    const char *o = get_host_target().os == Target::Windows ? ".obj" : ".o";

    std::vector<std::string> target_strings = {
        "host-profile-no_bounds_query",
        "host-profile",
    };

    std::vector<Target> targets;
    for (auto s : target_strings) {
        targets.emplace_back(s);
    }

    std::vector<std::string> files;

    // single-file outputs
    for (const char *ext : {".h", ".halide_generated.cpp", ".halide_compiler_log", ".py.cpp", ".pytorch.h", ".registration.cpp", ".schedule.h", a}) {
        if (do_object && !strcmp(ext, a)) continue;
        files.push_back(filename_prefix + ext);
    }
    if (do_object) {
        files.push_back(filename_prefix + "_runtime" + o);
        files.push_back(filename_prefix + "_wrapper" + o);
    }

    // multi-file outputs
    for (const auto &s : target_strings) {
        for (const char *ext : {".s", ".bc", ".featurization", ".ll", ".stmt", ".stmt.html", o}) {
            if (!do_object && !strcmp(ext, o)) continue;
            files.push_back(filename_prefix + "-" + s + ext);
        }
    }

    for (auto f : files) {
        Internal::ensure_no_file_exists(f);
    }

    // There isn't a public API that allows this directly, but Generators allow this
    // via command-line usage, so we'll test the internal API here.
    auto args = j.infer_arguments();
    auto module_producer = [&j, &args](const std::string &name, const Target &target) -> Module {
        return j.compile_to_module(args, name, target);
    };
    std::map<OutputFileType, std::string> outputs = {
        {OutputFileType::assembly, filename_prefix + ".s"},                        // IsMulti
        {OutputFileType::bitcode, filename_prefix + ".bc"},                        // IsMulti
        {OutputFileType::c_header, filename_prefix + ".h"},                        // IsSingle
        {OutputFileType::c_source, filename_prefix + ".halide_generated.cpp"},     // IsSingle
        {OutputFileType::compiler_log, filename_prefix + ".halide_compiler_log"},  // IsSingle
        // Note: compile_multitarget() doesn't produce cpp_stub output,
        // even if you pass this in.
        // {OutputFileType::cpp_stub, filename_prefix + ".stub.h"},  // IsSingle
        {OutputFileType::featurization, filename_prefix + ".featurization"},    // IsMulti
        {OutputFileType::llvm_assembly, filename_prefix + ".ll"},               // IsMulti
        {OutputFileType::object, filename_prefix + o},                          // IsMulti
        {OutputFileType::python_extension, filename_prefix + ".py.cpp"},        // IsSingle
        {OutputFileType::pytorch_wrapper, filename_prefix + ".pytorch.h"},      // IsSingle
        {OutputFileType::registration, filename_prefix + ".registration.cpp"},  // IsSingle
        {OutputFileType::schedule, filename_prefix + ".schedule.h"},            // IsSingle
        {OutputFileType::static_library, filename_prefix + a},                  // IsSingle
        {OutputFileType::stmt, filename_prefix + ".stmt"},                      // IsMulti
        {OutputFileType::stmt_html, filename_prefix + ".stmt.html"},            // IsMulti
    };
    if (do_object) {
        outputs.erase(OutputFileType::static_library);
    } else {
        outputs.erase(OutputFileType::object);
    }
    const CompilerLoggerFactory compiler_logger_factory =
        [](const std::string &, const Target &) -> std::unique_ptr<Internal::CompilerLogger> {
        // We don't care about the contents of the compiler log - only whether
        // it exists or not --  so just fill in with arbitrary strings.
        return std::unique_ptr<Internal::CompilerLogger>(new Internal::JSONCompilerLogger("generator_name", "function_name", "autoscheduler_name", Target(), "generator_args", false));
    };
    // The first argument to compile_multitarget is *function* name, not filename
    std::string function_name = leaf_name(filename_prefix);
    compile_multitarget(function_name, outputs, targets, target_strings, module_producer, compiler_logger_factory);

    for (auto f : files) {
        Internal::assert_file_exists(f);
    }
}

int main(int argc, char **argv) {
    Param<float> factor("factor");
    Func f, g, h, j;
    Var x, y;
    f(x, y) = x + y;
    g(x, y) = cast<float>(f(x, y) + f(x + 1, y));
    h(x, y) = f(x, y) + g(x, y);
    j(x, y) = h(x, y) * 2 * factor;

    f.compute_root();
    g.compute_root();
    h.compute_root();

    test_compile_to_static_library(j);
    test_compile_to_object_files(j);
    test_compile_to_object_files_no_runtime(j);
    test_compile_to_object_files_single_target(j);
    test_compile_to_everything(j, /*do_object*/ true);
    test_compile_to_everything(j, /*do_object*/ false);

    printf("Success!\n");
    return 0;
}
