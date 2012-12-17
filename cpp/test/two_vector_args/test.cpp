#include <Halide.h>
#include <sys/time.h>

using namespace Halide;

int main(int argc, char **argv) {

    Func f, g;
    Var x, y;
    
    g(x, y) = x+y;

    f(x, y) = g(x, x);
    
    f.vectorize(x, 4);

    Image<int> out = f.realize(4, 4);
    
    return 0;
}


