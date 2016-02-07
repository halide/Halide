#include "Halide.h"
using namespace Halide;

#include "benchmark.h"

Func bx, by;
Var x, y, xi, yi;
Image<uint16_t> in(6408, 4802);
Image<uint16_t> out(in.width()-16, in.height()-8);

uint16_t kw = 3,
         kh = 3;

void gen() {
    bx = Func("bx"); by = Func("by"); // reset
        
    // The algorithm
    Expr _bx = cast<uint16_t>(0);
    for (int i = 0; i < kw; i++) { _bx = _bx + in(x+i, y); }
    bx(x, y) = _bx/kw;

    Expr _by = cast<uint16_t>(0);
    for (int i = 0; i < kh; i++) { _by = _by + bx(x, y+i); }
    by(x, y) = _by/kh;
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
    
    if (argc > 1) {
        kw = atoi(argv[1]);
        if (argc > 2) {
            kh = atoi(argv[2]);
        } else {
            kh = kw;
        }
    }

    printf("# Size: %d x %d = %d megapixels\n",
                        out.width(),
                        out.height(),
                        out.width()*out.height()/1000000);
    printf("# Kernel: %d x %d\n", kw, kh);

TILED:
    for (int stripsize : {1, 2, 3, 4, 5, 6, 7, 8, 10, 16, 30, 32, 64, 96})
    {
        gen();
        by.tile(x, y, xi, yi, 256, stripsize).parallel(y).vectorize(xi, 8);
        bx.compute_at(by, x).vectorize(x, 8);
        printf("Tiled %dx%d\t%f\n", 256, stripsize, bench());
    }

LINEBUFFER:
    {
        gen();
        by.vectorize(x, 8);
        bx.store_root().compute_at(by, y).vectorize(x, 8);
        printf("Line buffered\t%f\n", bench());
    }


DARKROOM:
    {
        gen();
        by.split(x, x, xi, out.width()/4).reorder(xi, y, x)
          .parallel(x).vectorize(xi, 8);
        bx.store_at(by, x).compute_at(by, y).vectorize(x, 8);
        printf("Line buffered Darkroom\t%f\n", bench());
    }
    
LBSTRIPS:
    for (int stripsize : {7, 8, 9, 10, 16, 32, 64, 96, 128, 256, 512, 1024})
    {
        gen();
        by.split(y, y, yi, stripsize).parallel(y).vectorize(x, 8);
        bx.store_at(by, y).compute_at(by, yi).vectorize(x, 8);
        printf("Line buffered in strips %d\t%f\n", stripsize, bench());
    }

ROOTVEC:
    {
        gen();
        bx.compute_root().vectorize(x, 8);
        by.vectorize(x, 8);
        printf("Root vec\t%f\n", bench());
    }

INLINEVEC:
    {
        gen();
        by.vectorize(x, 8);
        printf("Inline vec\t%f\n", bench());
    }

ROOTPAR:
    {
        gen();
        bx.parallel(y, 8);
        by.parallel(y, 8);
        bx.compute_root();
        printf("Root par\t%f\n", bench());
    }

INLINEPAR:
    {
        gen();
        by.parallel(y, 8);
        printf("Inline par\t%f\n", bench());
    }

ROOTPARVEC:
    {
        gen();
        bx.parallel(y, 8).vectorize(x, 8);
        by.parallel(y, 8).vectorize(x, 8);
        bx.compute_root();
        printf("Root parvec\t%f\n", bench());
    }

INLINEPARVEC:
    {
        gen();
        by.parallel(y, 8).vectorize(x, 8);
        printf("Inline parvec\t%f\n", bench());
    }

ROOT:
    {
        gen();
        bx.compute_root();
        printf("Root\t%f\n", bench());
    }

INLINE:
    {
        gen();
        printf("Inline\t%f\n", bench());
    }
    
    return 0;
}
