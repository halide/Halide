#include <Halide.h>

using namespace Halide;

int main(int argc, char* argv[]) {
    Func g;

    {
        Func f;
        Var x, y;

        f(x, y) = x + y;

        g(x, y) = 0;
        g(x, y) += f(x, y);

        g.update().reorder(y, x);
        f.store_at(g, x).compute_at(g, y);
    }

    Target target = get_jit_target_from_environment();
    Halide::Buffer<int> out = g.realize(10, 10, target);

    for (int x = 0; x < out.width(); x++) {
        for (int y = 0; y < out.height(); y++) {
            const int actual = out(x,y);
            const int expected = x + y;
            if (actual != expected) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, actual, expected);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
