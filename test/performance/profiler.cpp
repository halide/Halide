#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int percentage = 0;
float ms = 0;
void my_print(void *, const char *msg) {
    float this_ms;
    int this_percentage;
    int val = sscanf(msg, " fn13: %fms (%d", &this_ms, &this_percentage);
    if (val == 2) {
        ms = this_ms;
        percentage = this_percentage;
    }
}

int main(int argc, char **argv) {
    // Make a long chain of finely-interleaved Funcs, of which one is very expensive.
    Func f[30];
    Var c, x;
    for (int i = 0; i < 30; i++) {
        f[i] = Func("fn" + std::to_string(i));
        if (i == 0) {
            f[i](c, x) = cast<float>(x + c);
        } else if (i == 13) {
            Expr e = f[i - 1](c, x);
            for (int j = 0; j < 200; j++) {
                e = sin(e);
            }
            f[i](c, x) = e;
        } else {
            f[i](c, x) = f[i - 1](c, x) * 2.0f;
        }
    }

    Func out;
    out(c, x) = 0.0f;
    const int iters = 100;
    RDom r(0, iters);
    out(c, x) += r * f[29](c, x);

    out.set_custom_print(&my_print);
    out.compute_root();
    out.update().reorder(c, x, r);
    for (int i = 0; i < 30; i++) {
        f[i].compute_at(out, x);
    }

    Target t = get_jit_target_from_environment().with_feature(Target::Profile);
    Buffer<float> im = out.realize(10, 1000, t);

    //out.compile_to_assembly("/dev/stdout", {}, t.with_feature(Target::JIT));

    printf("Time spent in fn13: %fms\n", ms);

    if (percentage < 40) {
        printf("Percentage of runtime spent in f13: %d\n"
               "This is suspiciously low. It should be more like 66%%\n",
               percentage);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
