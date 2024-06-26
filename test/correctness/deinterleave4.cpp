#include "Halide.h"
#include <stdio.h>

using namespace Halide;

Var x;

Func upsample(Func f) {
    Func u;
    u(x) = f(x / 2 + 1);
    return u;
}

Func build() {
    Func in;
    in(x) = x;
    in.compute_root();

    Func up = upsample(upsample(in));

    return up;
}

int main(int argc, char **argv) {
    Func f1 = build();
    Func f2 = build();
    f2.bound(x, 0, 64).vectorize(x);

    Buffer<int> o1 = f1.realize({64});
    Buffer<int> o2 = f2.realize({64});

    for (int x = 0; x < o2.width(); x++) {
        if (o1(x) != o2(x)) {
            printf("o1(%d) = %d but o2(%d) = %d\n", x, o1(x), x, o2(x));
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
