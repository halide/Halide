#include "Halide.h"
#include <sys/time.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x;
    Func f;

    UniformImage a(Int(32), 1);
    Image<int> b(1), c(1);
    a = c;

    f(x) = a(x) + b(x);

    for (int j = 0; j < 100; j++) {
        timeval t1, t2;
        gettimeofday(&t1, NULL);
        for (int i = 0; i < 10000; i++) {
            f.realize(1);
        }
        gettimeofday(&t2, NULL);
        printf("%d\n", (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec - t1.tv_usec));
    }    

    printf("Success!\n");
    return 0;
}
