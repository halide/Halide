#include <Halide.h>
using namespace Halide;

#include <iostream>

namespace Halide
{
	extern void testArray(Func f);
}

int main(int argc, char **argv) {

    Func f, g, h; Var x, y;
    
    h(x) = x;
    g(x) = h(x-1) + h(x+1);
    f(x, y) = (g(x-1) + g(x+1)) + y;

    std::cerr << f.rhs().pretty() << std::endl;
    
    Func ff = rehydrate(f.serialize(), f.name());
#if 0
	testArray(f);

    h.root();
    g.root();

    if (use_gpu()) {
        f.cudaTile(x, y, 16, 16);
        g.cudaTile(x, 128);
        h.cudaTile(x, 128);
    }
#endif
    
    Image<int> out = ff.realize(32, 32);

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
