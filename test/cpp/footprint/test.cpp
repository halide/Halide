#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {

    Func f, g, h; Var x, y;
    
    h(x, y) = x + y;
    g(x, y) = (h(x-1, y-1) + h(x-1, y-1))/2;
    f(x, y) = (g(x-1, y-1) + g(x+1, y+1))/2;

    if (f.footprint(g) != 9 || g.footprint(h) != 1) {
        printf("Footprints not computed correctly:\n");
        printf(" g in f: %d (should be 9)\n", f.footprint(g));
        printf(" h in g: %d (should be 1)\n", g.footprint(h));
        return -1;
    }

    printf("Success!\n");
    return 0;
}
