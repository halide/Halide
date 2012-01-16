#include "FImage.h"

using namespace FImage;

int main(int argc, char **argv) {
    Var x, y;

    Var bx("blockidx"), tx("threadidx"),
        by("blockidy"), ty("threadidy");

    // Single-dimensional tuples
    Func f1("one_d");
    f1(x, y) = (x, y);

    // TODO: change lower tuples not to reuse f1 - instead make new f1 via C function?
    if (use_gpu()) {
        f1.cudaTile(x, y, 16, 16);
    }

    Image<int> im = f1.realize(32, 32, 2);

    for (size_t x = 0; x < 32; x++) {
        for (size_t y = 0; y < 32; y++) {
            if (im(x, y, 0) != x ||
                im(x, y, 1) != y) {
                printf("im(%d, %d) = (%d, %d)\n", x, y, im(x, y, 0), im(x, y, 1));
                return -1;
            }            
        }
    }

    // Multi-dimensional tuples
    Func f2("two_d");
    Func fi;
    fi(x, y) = (x, y);
    f2(x, y) = (fi(x, y), fi(x, y)+17);
    if (use_gpu()) {
        f2.cudaTile(x, y, 16, 16);
    }
    Image<int> im2 = f2.realize(32, 32, 2, 2);
    for (size_t x = 0; x < 32; x++) {
        for (size_t y = 0; y < 32; y++) {
            int result[2][2] = {{im2(x, y, 0, 0), im2(x, y, 0, 1)},
                                {im2(x, y, 1, 0), im2(x, y, 1, 1)}}; 
            if (result[0][0] != x ||
                result[0][1] != y ||
                result[1][0] != x+17 ||
                result[1][1] != y+17) {
                printf("im2(%d %d) = ((%d, %d), (%d, %d))\n", 
                       x, y,
                       result[0][0], result[0][1],
                       result[1][0], result[1][1]);
                return -1;
            }
        }
    }

    // Putting the tuple dimension innermost
    Func f3("tuple_innermost"), f3a, f3b;
    f3a(x, y) = x;
    f3b(x, y) = y;
    f3 = (f3a, f3b);
    if (use_gpu()) {        
        Var x = f3.arg(1);
        Var y = f3.arg(2);
        f3.cudaTile(f3.arg(1), f3.arg(2), 16, 16);
    }
    Image<int> im3 = f3.realize(2, 32, 32);
    for (size_t x = 0; x < 32; x++) {
        for (size_t y = 0; y < 32; y++) {
            if (im3(0, x, y) != x ||
                im3(1, x, y) != y) {
                printf("im3(%d, %d) = (%d, %d)\n", x, y, im3(0, x, y), im3(1, x, y));
                return -1;
            }            
        }
    }

    // A pair of reductions
    Func f4("reduction_pair");
    RVar i(0, 10);
    f4(x, y) = (sum(x+i), product(x+y+i));
    Image<int> im4 = f4.realize(32, 32, 2);
    for (size_t x = 0; x < 32; x++) {
        for (size_t y = 0; y < 32; y++) {
            int correctSum = 0, correctProd = 1;
            for (size_t i = 0; i < 10; i++) {
                correctSum += x+i;
                correctProd *= x+y+i;
            }
            if (im4(x, y, 0) != correctSum ||
                im4(x, y, 1) != correctProd) {
                printf("im4(%d, %d) = (%d, %d) instead of (%d %d)\n",
                       x, y, im4(x, y, 0), im4(x, y, 1), correctSum, correctProd);
            }
        }
    }

    // Triples
    Func f5("triple");
    f5(x, y) = (x, y, x+y);
    if (use_gpu()) {
        f5.cudaTile(x, y, 16, 16);
    }
    Image<int> im5 = f5.realize(32, 32, 3);

    for (size_t x = 0; x < 32; x++) {
        for (size_t y = 0; y < 32; y++) {
            if (im5(x, y, 0) != x ||
                im5(x, y, 1) != y ||
                im5(x, y, 2) != x+y) {
                printf("im5(%d, %d) = (%d, %d, %d)\n", x, y, im5(x, y, 0), im5(x, y, 1), im5(x, y, 2));
                return -1;
            }            
        }
    }

    // Multi-dimensional tuple literals
    Func f6("two_d_tuple_literals");
    f6(x, y) = ((x+y, x*y), (x-y, x/(y+1)));
    if (use_gpu()) {
        f6.cudaTile(x, y, 16, 16);
    }
    Image<int> im6 = f6.realize(32, 32, 2, 2);
    for (size_t x = 0; x < 32; x++) {
        for (size_t y = 0; y < 32; y++) {
            if (im6(x, y, 0, 0) != x+y ||
                im6(x, y, 0, 1) != x*y ||
                im6(x, y, 1, 0) != x-y ||
                im6(x, y, 1, 1) != x/(y+1)) {
                printf("im6(%d, %d) = ((%d, %d), (%d, %d))\n", x, y, 
                       im6(x, y, 0, 0), im6(x, y, 0, 1),
                       im6(x, y, 1, 0), im6(x, y, 1, 1));
                return -1;
            }            
        }
    }

    // Tuples inside reductions
    Func f7("tuple_inside_reduce");
    f7(x, y) = sum((x*i, y*i+1));
    Image<int> im7 = f7.realize(32, 32, 2);
    for (size_t x = 0; x < 32; x++) {
        for (size_t y = 0; y < 32; y++) {
            int correct[] = {0, 0};
            for (size_t i = 0; i < 10; i++) {
                correct[0] += x*i;
                correct[1] += y*i+1;
            }
            if (im7(x, y, 0) != correct[0] ||
                im7(x, y, 1) != correct[1]) {
                printf("im7(%d, %d) = (%d, %d) instead of (%d %d)\n", 
                       x, y, 
                       im7(x, y, 0), im7(x, y, 1),
                       correct[0], correct[1]);
            }
        }
    }

    printf("Success!\n");
    return 0;
}
