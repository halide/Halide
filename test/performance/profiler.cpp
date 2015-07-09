#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int percentage = 0;
float ms = 0;
void my_print(void *, const char *msg) {
    float this_ms;
    int this_percentage;
    int val = sscanf(msg, " f13: %fms (%d", &this_ms, &this_percentage);
    if (val == 2) {
        ms = this_ms;
        percentage = this_percentage;
    }
}

int main(int argc, char **argv) {
    // Make a long chain of finely-interleaved Funcs, of which one is very expensive.
    Func f[30];
    Var x;
    for (int i = 0; i < 30; i++) {
        f[i] = Func("f" + std::to_string(i));
        if (i == 0) {
            f[i](x) = cast<float>(x);
        } else if (i == 13) {
            Expr e = f[i-1](x);
            for (int j = 0; j < 200; j++) {
                e = sin(e);
            }
            f[i](x) = e;
        } else {
            f[i](x) = f[i-1](x)*2.0f;
        }
    }

    Func out;
    out(x) = 0.0f;
    const int iters = 1000;
    RDom r(0, iters);
    out(x) += r*f[29](x);

    out.set_custom_print(&my_print);
    out.compute_root();
    out.update().reorder(x, r);
    for (int i = 0; i < 30; i++) {
        f[i].compute_at(out, x);
    }

    Target t = get_jit_target_from_environment().with_feature(Target::Profile);
    Image<float> im = out.realize(1000, t);

    printf("Time spent in f13: %fms\n", ms);

    if (percentage < 40) {
        printf("Percentage of runtime spent in f13: %d\n"
               "This is suspiciously low. It should be more like 66%%\n",
               percentage);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
