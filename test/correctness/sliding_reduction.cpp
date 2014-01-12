#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int counter = 0;
extern "C" int call_count(int x) {
    counter++;
    assert(counter > 0);
    return 99;
}
HalideExtern_1(int, call_count, int);

void check(Image<int> im) {
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = 99*3;
            if (im(x, y) != correct) {
                printf("Value at %d %d was %d instead of %d\n", 
                       x, y, im(x, y), correct);
                exit(-1);
            }
        }
    }
}

int main(int argc, char **argv) {

    Var x, y;

    {
        // Could slide this reduction over y, but we don't, because it's
        // too hard to implement bounds analysis on the intermediate
        // stages.
        Func f("f");
        f(x, y) = x;
        f(0, y) += f(1, y) + f(0, y);
        f(x, y) = call_count(f(x, y));
        
        Func g("g");
        g(x, y) = f(x, y) + f(x, y-1) + f(x, y-2);
        
        f.store_root().compute_at(g, y);

        counter = 0;
        check(g.realize(2, 10));
        
        if (counter != 60) { // Would be 24 if we could slide it
            printf("Failed sliding a reduction: %d\n", counter);
            return -1;
        }
    }

    {
        // Can't slide this reduction over y, because the second stage scatters.
        Func f("f");
        f(x, y) = x;
        f(x, x) += f(x, 0) + f(x, 1);
        f(x, y) = call_count(f(x, y));
        
        Func g("g");
        g(x, y) = f(x, y) + f(x, y-1) + f(x, y-2);
        
        f.store_root().compute_at(g, y);

        counter = 0;
        check(g.realize(2, 10));
        
        if (counter != 60) {
            printf("Failed sliding a reduction: %d\n", counter);
            return -1;
        }
    }

    {
        // Would be able to slide this, but the unroll in the first
        // stage forces evaluations of size two in y, which would
        // clobber earlier values.
        Func f("f");
        f(x, y) = x;
        f(0, y) += f(1, y) + f(2, y);
        f(x, y) = call_count(f(x, y));
        
        f.unroll(y, 2);

        Func g("g");
        g(x, y) = f(x, y) + f(x, y-1) + f(x, y-2);
        
        f.store_root().compute_at(g, y);

        counter = 0;
        check(g.realize(2, 10));
        
        if (counter != 60) {
            printf("Failed sliding a reduction: %d\n", counter);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
