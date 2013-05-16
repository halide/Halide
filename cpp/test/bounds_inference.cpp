#include <stdio.h>
#include <Halide.h>
using namespace Halide;

int main(int argc, char **argv) {

    Func f, g, h; Var x, y;
    
    h(x) = x;
    g(x) = h(x-1) + h(x+1);
    f(x, y) = (g(x-1) + g(x+1)) + y;

    h.compute_root();
    g.compute_root();

    char *target = getenv("HL_TARGET");
    if (target && std::string(target) == "ptx") {
        f.cuda_tile(x, y, 16, 16);
        g.cuda_tile(x, 128);
        h.cuda_tile(x, 128);
    }
    

    Image<int> out = f.realize(32, 32);

    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            if (out(x, y) != x*4 + y) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), x*4+y);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
