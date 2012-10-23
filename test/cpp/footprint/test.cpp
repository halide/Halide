#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {

    Func f, g, h; Var x, y;
    
    h(x, y) = x + y;
    g(x, y) = (h(x-1, y-1) + h(x-1, y-1))/2;
    f(x, y) = (g(x-1, y-1) + g(x+1, y+1))/2;

    std::vector<int> gInF = f.footprint(g);
    std::vector<int> hInG = g.footprint(h);
    if (gInF.size() != 2 || gInF[0] != 3 || gInF[1] != 3 ||
        hInG.size() != 2 || hInG[0]*hInG[1] != 1)
    {
        printf("Footprints not computed correctly:\n");
        printf(" g in f: %d %d (should be 3x3)\n", gInF[0], gInF[1]);
        printf(" h in g: %d %d (should be 1x1)\n", hInG[0], hInG[1]);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
