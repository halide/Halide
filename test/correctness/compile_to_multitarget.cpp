#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;

std::string get_fname(const std::string &base) {
    return Internal::get_test_tmp_dir() + "halide_test_correctness_compile_to_multitarget_" + base;
}

void test_compile_to_static_library(Func j) {
    std::string fname = get_fname("c1");
    const char *a = get_host_target().os == Target::Windows ? ".lib" : ".a";

    std::vector<Target> targets = {
        Target("host-profile-no_bounds_query"),
        Target("host-profile"),
    };

    std::vector<std::string> files;
    files.push_back(fname + ".h");
    files.push_back(fname + a);

    for (auto f : files) {
        Internal::ensure_no_file_exists(f);
    }

    j.compile_to_multitarget_static_library(fname, j.infer_arguments(), targets);

    for (auto f : files) {
        Internal::assert_file_exists(f);
    }

    // TODO: would be nice to examine the contents of the library and verify the
    // sub-objects have the filenames we expect.
}

void test_compile_to_object_files(Func j) {
    std::string fname = get_fname("c2");
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
    files.push_back(fname + ".h");
    files.push_back(fname + "_runtime" + o);
    files.push_back(fname + "_wrapper" + o);
    for (auto s : target_strings) {
        files.push_back(fname + "-" + s + o);
    }

    for (auto f : files) {
        Internal::ensure_no_file_exists(f);
    }

    j.compile_to_multitarget_object_files(fname, j.infer_arguments(), targets, target_strings);

    for (auto f : files) {
        Internal::assert_file_exists(f);
    }
}

void test_compile_to_object_files_no_runtime(Func j) {
    std::string fname = get_fname("c3");
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
    files.push_back(fname + ".h");
    files.push_back(fname + "_wrapper" + o);
    for (auto s : target_strings) {
        files.push_back(fname + "-" + s + o);
    }

    for (auto f : files) {
        Internal::ensure_no_file_exists(f);
    }

    j.compile_to_multitarget_object_files(fname, j.infer_arguments(), targets, target_strings);

    for (auto f : files) {
        Internal::assert_file_exists(f);
    }
}

void test_compile_to_object_files_single_target(Func j) {
    std::string fname = get_fname("c4");
    const char *o = get_host_target().os == Target::Windows ? ".obj" : ".o";

    std::vector<std::string> target_strings = {
        "host-debug",
    };

    std::vector<Target> targets;
    for (auto s : target_strings) {
        targets.emplace_back(s);
    }

    std::vector<std::string> files;
    files.push_back(fname + ".h");
    files.push_back(fname + o);

    for (auto f : files) {
        Internal::ensure_no_file_exists(f);
    }

    j.compile_to_multitarget_object_files(fname, j.infer_arguments(), targets, target_strings);

    for (auto f : files) {
        Internal::assert_file_exists(f);
    }
}

void test_compile_to_everything(Func j, bool do_object) {
    std::string fname = get_fname(do_object ? "c5" : "c6");
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
        files.push_back(fname + ext);
    }
    if (do_object) {
        files.push_back(fname + "_runtime" + o);
        files.push_back(fname + "_wrapper" + o);
    }

    // multi-file outputs
    for (const auto &s : target_strings) {
        for (const char *ext : {".s", ".bc", ".featurization", ".ll", ".stmt", ".stmt.html", o}) {
            if (!do_object && !strcmp(ext, o)) continue;
            files.push_back(fname + "-" + s + ext);
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
    std::map<Output, std::string> outputs = {
        {Output::assembly, fname + ".s"},                        // IsMulti
        {Output::bitcode, fname + ".bc"},                        // IsMulti
        {Output::c_header, fname + ".h"},                        // IsSingle
        {Output::c_source, fname + ".halide_generated.cpp"},     // IsSingle
        {Output::compiler_log, fname + ".halide_compiler_log"},  // IsSingle
        // Note: compile_multitarget() doesn't produce cpp_stub output,
        // even if you pass this in.
        // {Output::cpp_stub, fname + ".stub.h"},  // IsSingle
        {Output::featurization, fname + ".featurization"},    // IsMulti
        {Output::llvm_assembly, fname + ".ll"},               // IsMulti
        {Output::object, fname + o},                          // IsMulti
        {Output::python_extension, fname + ".py.cpp"},        // IsSingle
        {Output::pytorch_wrapper, fname + ".pytorch.h"},      // IsSingle
        {Output::registration, fname + ".registration.cpp"},  // IsSingle
        {Output::schedule, fname + ".schedule.h"},            // IsSingle
        {Output::static_library, fname + a},                  // IsSingle
        {Output::stmt, fname + ".stmt"},                      // IsMulti
        {Output::stmt_html, fname + ".stmt.html"},            // IsMulti
    };
    if (do_object) {
        outputs.erase(Output::static_library);
    } else {
        outputs.erase(Output::object);
    }
    const CompilerLoggerFactory compiler_logger_factory =
        [](const std::string &, const Target &) -> std::unique_ptr<Internal::CompilerLogger> {
        // We don't care about the contents of the compiler log - only whether
        // it exists or not --  so just fill in with arbitrary strings.
        return std::unique_ptr<Internal::CompilerLogger>(new Internal::JSONCompilerLogger("generator_name", "function_name", "autoscheduler_name", Target(), "generator_args", false));
    };
    compile_multitarget(fname, outputs, targets, target_strings, module_producer, compiler_logger_factory);

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
