#include "Halide.h"
#include <stdio.h>

#include "test/common/halide_test_dirs.h"

using namespace Halide;


int main(int argc, char **argv) {
    // Make sure it's possible to generate object files for lots of
    // targets. This provides early warning that you may have broken
    // Halide on some other platform.

    Func f("f");
    Var x;
    f(x) = x;

    std::string targets[] = {
        "arm-32-linux",
        "arm-64-linux",

//        "arm-64-android",
        "arm-32-android",
        "arm-32-ios",
        // "x86-64-linux",
        // "x86-32-linux",
        "x86-64-osx",
        "x86-32-osx",
        // "x86-64-windows",
        // "x86-32-windows",
//        "arm-64-ios",
        // "mips-32-android"
    };

    for (const std::string &t : targets) {
        Target target(t);
        if (!target.supported()) continue;

std::cerr << "Test generating: "<<target<<"\n";
        std::string object_name = Internal::get_test_tmp_dir() + "test_object_" + t;
        std::string lib_name = Internal::get_test_tmp_dir() + "test_lib_" + t;
        if (target.os == Target::Windows && !target.has_feature(Target::MinGW)) {
            object_name += ".obj";
            lib_name += ".lib";
        } else {
            object_name += ".o";
            lib_name += ".a";
        }

        Internal::ensure_no_file_exists(object_name);
        Internal::ensure_no_file_exists(lib_name);

        f.compile_to_file(Internal::get_test_tmp_dir() + "test_object_" + t, std::vector<Argument>(), "", target);
        f.compile_to_static_library(Internal::get_test_tmp_dir() + "test_lib_" + t, std::vector<Argument>(), "", target);

        Internal::assert_file_exists(object_name);
        Internal::assert_file_exists(lib_name);
    }

    printf("Success!\n");
    return 0;
}
