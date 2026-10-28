#include "Halide.h"

#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // Precomputing a Func that computes floating-point values while
    // cross-compiling warns that the host may not compute exactly what the
    // target would.
    Var x("x");
    Func lut("lut"), g("g");
    lut(x) = sin(cast<float>(x) * 0.1f);
    g(x) = lut(x % 16);
    lut.precompute();

    Target target = get_host_target().arch == Target::X86 ? Target("arm-64-linux") : Target("x86-64-linux");
    g.compile_to_module({}, "g", target);

    printf("Success!\n");
    return 0;
}
