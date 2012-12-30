#include "Halide.h"
#include <sys/time.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x;

    ImageParam a(Int(32), 1);
    Image<int> b(1), c(1);
    b(0) = 17;
    c(0) = 0;
    a.set(c);


    timeval t1, t2;
    gettimeofday(&t1, NULL);

    for (int i = 0; i < 100; i++) {
        Func f;
        f(x) = a(x) + b(x);
        f.realize(c);
        assert(c(0) == (i+1)*17);
    }    

    gettimeofday(&t2, NULL);
    int elapsed = int(t2.tv_sec - t1.tv_sec);
    elapsed = 1000000*elapsed + int(t2.tv_usec - t1.tv_usec);

    printf("%d us per jit compilation\n", elapsed/100);

    printf("Success!\n");
    return 0;
}
