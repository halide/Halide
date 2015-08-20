#include "Halide.h"
#include <stdio.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif

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
        Target target = parse_target_string(t);
        f.compile_to_file("test_object_" + t, std::vector<Argument>(), target);

        #ifndef _MSC_VER
        std::string object_name = "test_object_" + t + ".o";
        if (target.os == Target::Windows) object_name += "bj";
        assert(access(object_name.c_str(), F_OK) == 0 && "Output file not created.");
        #endif
    }

    printf("Success!\n");
    return 0;
}
