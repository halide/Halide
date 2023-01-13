#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;

int main(int argc, char **argv) {
    // Make sure it's possible to generate object files for lots of
    // targets. This provides early warning that you may have broken
    // Halide on some other platform.

    // We test -d3d12compute for 64-bit Windows platforms
    // due to the peculiar required mixture of calling conventions.

    Func f("f");
    Var x;
    f(x) = x;

    std::string targets[] = {
        "arm-32-android",
        "arm-32-ios",
        "arm-32-linux",
        "arm-32-noos-semihosting",
        "arm-64-android",
        "arm-64-ios",
        "arm-64-linux",
        "arm-64-windows",
        "arm-64-windows-d3d12compute",
        "arm-64-noos-semihosting",
        "x86-32-linux",
        "x86-32-osx",
        "x86-32-windows",
        "x86-64-linux",
        "x86-64-osx",
        "x86-64-windows",
        "x86-64-windows-d3d12compute",
        "wasm-32-wasmrt",
    };

    for (const std::string &t : targets) {
        Target target(t);
        if (!target.supported()) continue;

        std::cout << "Test generating: " << target << "\n";
        std::string object_name = Internal::get_test_tmp_dir() + "test_object_" + t;
        std::string lib_name = Internal::get_test_tmp_dir() + "test_lib_" + t;
        if (target.os == Target::Windows) {
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
