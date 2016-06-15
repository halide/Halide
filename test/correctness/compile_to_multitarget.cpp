#include "Halide.h"
#include <stdio.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

using namespace Halide;

void testCompileToOutput(Func j) {
    std::string fn_object = "compile_to_multitarget";

    if (Internal::file_exists(fn_object)) { Internal::file_unlink(fn_object); }
    assert(!Internal::file_exists(fn_object) && "Output file already exists.");

    std::vector<Target> targets = {
        Target("host-profile-debug"),
        Target("host-profile"),
    };
    j.compile_to_multitarget_static_library(fn_object, j.infer_arguments(), targets);
#ifdef _MSC_VER
    std::string expected_lib = fn_object + ".lib";
#else
    std::string expected_lib = fn_object + ".a";
#endif
    std::string expected_h = fn_object + ".h";

    assert(Internal::file_exists(expected_lib) && "Output lib not created.");
    assert(Internal::file_exists(expected_h) && "Output h not created.");
}

int main(int argc, char **argv) {
    Param<float> factor("factor");
    Func f, g, h, j;
    Var x, y;
    f(x, y) = x + y;
    g(x, y) = cast<float>(f(x, y) + f(x+1, y));
    h(x, y) = f(x, y) + g(x, y);
    j(x, y) = h(x, y) * 2 * factor;

    f.compute_root();
    g.compute_root();
    h.compute_root();

    testCompileToOutput(j);

    printf("Success!\n");
    return 0;
}
