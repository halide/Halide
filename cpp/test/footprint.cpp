#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {

    Func f, g, h; Var x, y;
    
    UniformImage im(Float(32), 2);
    h(x, y) = im(x, y);
    g(x, y) = (h(x, y-1) + h(x, y) + h(x, y+1))/3;
    f(x, y) = (g(x-1, y) + g(x, y) + g(x+1, y))/3;

    std::vector<int> gInF = f.footprint(g);
    std::vector<int> hInG = g.footprint(h);
    /*
    printf("%d %d\n"
           "%d %d\n",
           gInF[0], gInF[1],
           hInG[0], hInG[1]);
    */
    if (gInF.size() != 2 || gInF[0] != 3 || gInF[1] != 1 ||
        hInG.size() != 2 || hInG[0] != 1 || hInG[1] != 3)
    {
        printf("Footprints not computed correctly:\n");
        printf(" g in f: %d %d (should be 3x3)\n", gInF[0], gInF[1]);
        printf(" h in g: %d %d (should be 1x1)\n", hInG[0], hInG[1]);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
