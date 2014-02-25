#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int main() {
    Func f;
    Var x, y, c;

    f(x, y, c) = cast<uint8_t>(select(c == 0, 255,
                                      select(c == 1, 127, 12)));

    Image<uint8_t> out(10, 10, 3);
    f.reorder(c, x, y);
    f.bound(c, 0, 3);
    f.unroll(c);
    f.glsl(x, y, c);
    f.realize(out);


    out.copy_to_host();
//    printf("dev_dirty=%d\n",
    printf("%d %d %d\n", out(0, 0, 0), out(0, 0, 1), out(0, 0, 2));

    printf("Finished!\n");
}
