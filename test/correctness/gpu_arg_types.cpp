#include "Halide.h"

using namespace Halide;
int main(int argc, char *argv[]) {

    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    Func f, g;
    Var x, y, tx, ty;
    Param<int16_t> foo;

    Expr e = select(foo > x, cast<int16_t>(255), foo + cast<int16_t>(x));
    f(x) = e;
    g(x) = e;

    foo.set(-1);
    f.gpu_tile(x, tx, 8);

    Buffer<int16_t> out = f.realize({256});
    Buffer<int16_t> out2 = g.realize({256});
    out.copy_to_host();

    for (int i = 0; i < 256; i++) {
        if (out(i) != out2(i)) {
            printf("Incorrect result at %d: %d != %d\n", i, out(i), out2(i));
            printf("Failed\n");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
