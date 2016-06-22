#include "Halide.h"
#include <stdio.h>

using namespace Halide;


int main(int argc, char **argv) {
    // Make sure it's possible to generate object files for lots of
    // targets. This provides early warning that you may have broken
    // Halide on some other platform.

    Func f;
    Var x;
    f(x) = x;

    std::string targets[] = {
        "x86-64-linux",
        "x86-32-linux",
        "x86-64-osx",
        "x86-32-osx",
        "x86-64-windows",
        "x86-32-windows",
        "arm-64-ios",
        "arm-32-ios",
        "arm-64-android",
        "arm-32-android",
        "mips-32-android"
    };

    for (const std::string &t : targets) {
        Target target(t);
        if (!target.supported()) continue;
        f.compile_to_file("test_object_" + t, std::vector<Argument>(), target);
        f.compile_to_static_library("test_lib_" + t, std::vector<Argument>(), target);

        std::string object_name = "test_object_" + t;
        std::string lib_name = "test_lib_" + t;
        if (target.os == Target::Windows && !target.has_feature(Target::MinGW)) {
            object_name += ".obj";
            lib_name += ".lib";
        } else {
            object_name += ".o";
            lib_name += ".a";
        }
        assert(Internal::file_exists(object_name) && "Output file not created.");
    }

    printf("Success!\n");
    return 0;
}
