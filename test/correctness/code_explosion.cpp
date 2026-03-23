#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x;
    const int size = 100;

    // Try a nest of highly connected funcs all marked inline.
    std::vector<Func> funcs;
    funcs.push_back(lambda(x, cast<uint32_t>(x)));
    funcs.push_back(lambda(x, cast<uint32_t>(x)));
    for (int i = 2; i < size; i++) {
        funcs.push_back(lambda(x, funcs[i - 1](x) + funcs[i - 2](x)));
    }
    Func g;
    g(x) = funcs[funcs.size() - 1](x);
    g.realize({10});

    // Test a nest of highly connected exprs. Compilation will barf if
    // this gets expanded into a tree.
    Func f;
    std::vector<Expr> e(size);

    e[0] = cast<uint32_t>(x);
    e[1] = cast<uint32_t>(x);
    for (size_t i = 2; i < e.size(); i++) {
        e[i] = e[i - 1] + e[i - 2];
    }

    f(x) = e[e.size() - 1];

    f.realize({10});

    printf("Success!\n");
    return 0;
}
