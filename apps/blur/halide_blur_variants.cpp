#include "Halide.h"
using namespace Halide;

#include "benchmark.h"

Func bx, by;
Var x, y, xi, yi;
Image<uint16_t> in(6408, 4802);
Image<uint16_t> out(in.width()-16, in.height()-8);

void gen() {
    bx = Func("bx"); by = Func("by"); // reset
        
    // The algorithm
    bx(x, y) = (in(x, y)
              + in(x+1, y)
              + in(x+2, y)
              + in(x+3, y)
              + in(x+4, y)
              + in(x+5, y)
              + in(x+6, y)
    )/7;
    by(x, y) = (bx(x, y)
              + bx(x, y+1)
              + bx(x, y+2)
              + bx(x, y+3)
              + bx(x, y+4)
              + bx(x, y+5)
              + bx(x, y+6)
    )/7;
}

double bench() {
    by.compile_jit();
    
    fprintf(stderr, "\n———————————————————————————————————————————————\n");
    by.print_loop_nest();
    fprintf(stderr, "\n");
    
    return benchmark(10, 1, [&]() {
        by.realize(out);
    });
}

int main(int argc, char **argv) {

    for (int y = 0; y < in.height(); y++) {
        for (int x = 0; x < in.width(); x++) {
            in(x, y) = rand() & 0xfff;
        }
    }

    fprintf(stderr, "Size: %d x %d = %d megapixels\n",
                        out.width(),
                        out.height(),
                        out.width()*out.height()/1000000);

    {
        gen();
        bx.compute_root();
        printf("Root\t%f\n", bench());
    }

    {
        gen();
        printf("Inline\t%f\n", bench());
    }

    {
        gen();
        bx.parallel(y, 8).vectorize(x, 8);
        by.parallel(y, 8).vectorize(x, 8);
        bx.compute_root();
        printf("Root parallel\t%f\n", bench());
    }

    {
        gen();
        by.parallel(y, 8).vectorize(x, 8);
        printf("Inline parallel\t%f\n", bench());
    }

    {
        gen();
        by.tile(x, y, xi, yi, 256, 32).parallel(y).vectorize(xi, 8);
        bx.compute_at(by, x).vectorize(x, 8);
        printf("Tiled\t%f\n", bench());
    }

    {
        gen();
        by.split(x, x, xi, out.width()/4).reorder(xi, y, x)
          .parallel(x).vectorize(xi, 8);
        bx.store_at(by, x).compute_at(by, y).vectorize(x, 8);
        printf("Line buffered Darkroom\t%f\n", bench());
    }
    
    {
        gen();
        by.split(y, y, yi, 8).parallel(y).vectorize(x, 8);
        bx.store_at(by, y).compute_at(by, yi).vectorize(x, 8);
        printf("Line buffered chunks\t%f\n", bench());
    }
    
    return 0;
}
