#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    const int n_types = 9;

    Type types[] = {Int(8), Int(16), Int(32), Int(64),
                    UInt(8), UInt(16), UInt(32), UInt(64),
                    Float(32)};
    Func funcs[n_types];

    Var x;

    Func out;

    Expr e = 0;
    for (int i = 0; i < n_types; i++) {
        funcs[i](x) = cast(types[i], x + i);
        e += cast<int>(funcs[i](x));
        funcs[i].compute_at(out, Var::gpu_blocks()).gpu_threads(x);
    }
    

    out(x) = e;
    out.gpu_tile(x, 23);

    Image<int> output = out.realize(23*5);
    for (int x = 0; x < output.width(); x++) {
        int correct = n_types * x + (n_types * (n_types - 1)) / 2;
        if (output(x) != correct) {
            printf("output(%d) = %d instead of %d\n", 
                   x, output(x), correct);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
