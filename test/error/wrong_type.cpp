#include "Halide.h"
using namespace Halide;

#ifdef NDEBUG
#error "wrong_type requires assertions"
#endif

int main(int argc, char **argv) {
    Func f;
    Var x;
    f(x) = x;
    Buffer<float> im = f.realize(100);

    printf("Success!\n");
    return 0;
}
