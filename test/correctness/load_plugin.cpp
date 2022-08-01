#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s path-to-adams2019 path-to-li2018 path-to-mullapudi2016\n", argv[0]);
        exit(-1);
    }

    // Use a fixed target for the analysis to get consistent results from this test.
    Target target("x86-64-linux-sse41-avx-avx2");

    const char* autoscheduler_names[3] = {
        "Adams2019",
        "Li2018",
        "Mullapudi2016",
    };

    // The entire point of this test is to ensure that plugins are built with the equivalent
    // of -rdynamic linking vs libHalide (i.e., they will never attempt to load their own copies).
    // Failure to do so can end up with the plugin's libHalide having a separate set of global vars,
    // meaning that the global list of available autoschedulers is never set in the 'host' libHalide,
    // so calls to apply_autoscheduler() would fail in that case.
    for (int i = 0; i < 3; i++) {
        Func f("f");
        Var x("x"), y("y");
        f(x, y) = x + y;
        f.set_estimates({{0, 256}, {0, 256}});
        Pipeline p(f);

        printf("Loading: %s\n", argv[i+1]);
        load_plugin(argv[i+1]);

        p.apply_autoscheduler(target, {autoscheduler_names[i], {}});
    }

    printf("Success!\n");
    return 0;
}
