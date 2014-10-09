#include <Halide.h>
#include <stdio.h>

using namespace Halide;

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

extern "C" DLLEXPORT int count(int x) {
    static int call_counter = 0;
    return call_counter++;
}
HalideExtern_1(int, count, int);

int main(int argc, char **argv) {
    Func f, g;
    Var x;
    RDom r(0, 10);
    RVar ri, ro;
    f(x) = count(x);
    g(x) = 0;
    g(r) = f(r);

    g.bound(x, 0, 10);
    g.update().split(r, ro, ri, 2);
    f.compute_at(g, ri);

    Image<int> im = g.realize(10);

    for (int i = 0; i < im.width(); i++) {
        if (im(i) != i) {
            printf("im(%d) = %d instead of %d\n", i, im(i), i);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
