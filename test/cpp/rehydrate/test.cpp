#include <Halide.h>
using namespace Halide;

#include <iostream>

namespace Halide
{
	extern void testArray(Func f);
}

using namespace std;
int main(int argc, char **argv) {

    int width = 32;

    Func f("f"), g("g"), h("h"); Var x("x"), y("y");

    Uniform<int> offset("offset");
    offset = 666;

    Image<int> in(width+4);
    for (int i=0; i < in.width(); i++) in(i) = rand() / 2;
    
    h(x) = in(clamp(x, 0, in.width()));
    g(x) = h(x-1 + offset - *(int*)offset.data()) + h(x+1 + offset - *(int*)offset.data());
    f(x, y) = (g(x-1) + g(x+1)) + y;

    // Rehydrate ff by serializing then deserializing the pipeline out through f
    Func ff = rehydrate(f.serialize(), f.name());

    // reassign the uniform/image inputs
    ff.uniforms()[0].set(*(int*)offset.data());
    ff.uniformImages()[0] = in;

    f.funcs()[0].root();
    f.funcs()[1].root();
    ff.funcs()[0].root();
    ff.funcs()[1].root();

    if (use_gpu()) {
        f.cudaTile(x, y, 16, 16);
        ff.cudaTile(x, y, 16, 16);
    } else {
        Var xi("xi"), yi("yi");
        f.tile(x, y, xi, yi, 16, 16);
        ff.tile(x, y, xi, yi, 16, 16);
    }
    
    Image<int> out = ff.realize(width, width);
    Image<int> ref = f.realize(width, width);

    for (int y = 0; y < width; y++) {
        for (int x = 0; x < width; x++) {
            if (out(x, y) != ref(x, y)) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), ref(x, y));
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
