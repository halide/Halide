#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;

void test_compile_to_static_library(Func j) {
    std::string fn_object = Internal::get_test_tmp_dir() + "c1";
    std::string a = get_host_target().os == Target::Windows ? ".lib" : ".a";

    std::vector<Target> targets = {
        Target("host-profile-no_bounds_query"),
        Target("host-profile"),
    };

    std::vector<std::string> files;
    files.push_back(fn_object + ".h");
    files.push_back(fn_object + a);

    for (auto f : files) {
        Internal::ensure_no_file_exists(f);
    }

    j.compile_to_multitarget_static_library(fn_object, j.infer_arguments(), targets);

    for (auto f : files) {
        Internal::assert_file_exists(f);
    }
}

void test_compile_to_object_files(Func j) {
    std::string fn_object = Internal::get_test_tmp_dir() + "c2";
    std::string o = get_host_target().os == Target::Windows ? ".obj" : ".o";

    std::vector<std::string> target_strings = {
        "host-profile-no_bounds_query",
        "host-profile",
    };

    std::vector<Target> targets;
    for (auto s : target_strings) {
        targets.emplace_back(s);
    }

    std::vector<std::string> files;
    files.push_back(fn_object + ".h");
    files.push_back(fn_object + "_runtime" + o);
    files.push_back(fn_object + "_wrapper" + o);
    for (auto s : target_strings) {
        files.push_back(fn_object + "_" + Internal::replace_all(s, "-", "_") + o);
    }

    for (auto f : files) {
        Internal::ensure_no_file_exists(f);
    }

    j.compile_to_multitarget_object_files(fn_object, j.infer_arguments(), targets, target_strings);

    for (auto f : files) {
        Internal::assert_file_exists(f);
    }
}

void test_compile_to_object_files_no_runtime(Func j) {
    std::string fn_object = Internal::get_test_tmp_dir() + "c3";
    std::string o = get_host_target().os == Target::Windows ? ".obj" : ".o";

    std::vector<std::string> target_strings = {
        "host-profile-no_bounds_query-no_runtime",
        "host-profile-no_runtime",
    };

    std::vector<Target> targets;
    for (auto s : target_strings) {
        targets.emplace_back(s);
    }

    std::vector<std::string> files;
    files.push_back(fn_object + ".h");
    files.push_back(fn_object + "_wrapper" + o);
    for (auto s : target_strings) {
        files.push_back(fn_object + "_" + Internal::replace_all(s, "-", "_") + o);
    }

    for (auto f : files) {
        Internal::ensure_no_file_exists(f);
    }

    j.compile_to_multitarget_object_files(fn_object, j.infer_arguments(), targets, target_strings);

    for (auto f : files) {
        Internal::assert_file_exists(f);
    }
}

void test_compile_to_object_files_single_target(Func j) {
    std::string fn_object = Internal::get_test_tmp_dir() + "c4";
    std::string o = get_host_target().os == Target::Windows ? ".obj" : ".o";

    std::vector<std::string> target_strings = {
        "host",
    };

    std::vector<Target> targets;
    for (auto s : target_strings) {
        targets.emplace_back(s);
    }

    // compile_to_multitarget_object_files() with just a single target
    // should act just like compile_to_file()
    std::vector<std::string> files;
    files.push_back(fn_object + ".h");
    files.push_back(fn_object + o);

    for (auto f : files) {
        Internal::ensure_no_file_exists(f);
    }

    j.compile_to_multitarget_object_files(fn_object, j.infer_arguments(), targets, target_strings);

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

    printf("Success!\n");
    return 0;
}
