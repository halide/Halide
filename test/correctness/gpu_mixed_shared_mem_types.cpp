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

    Expr e = cast<uint64_t>(0);
    int offset = 0;
    for (int i = 0; i < n_types; i++) {
        int off = 0;
        if ((types[i].is_int() || types[i].is_uint()) && 
            types[i].bits <= 64) {
            off = (1 << (types[i].bits - 4)) + 17;
        }
        offset += off;

        funcs[i](x) = cast(types[i], x/16 + off);
        e += cast<uint64_t>(funcs[i](x));
        funcs[i].compute_at(out, Var::gpu_blocks()).gpu_threads(x);
    }
    

    out(x) = e;
    out.gpu_tile(x, 23);

    Image<uint64_t> output = out.realize(23*5);
    for (uint64_t x = 0; x < output.width(); x++) {
        uint64_t correct = n_types * (x / 16) + offset;
        if (output(x) != correct) {
            printf("output(%lld) = %lld instead of %lld\n", 
                   x, output(x), correct);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
