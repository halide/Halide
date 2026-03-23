#include "Halide.h"

using namespace Halide;

uint64_t fn(int i0, int i1, int i2, int i3, int i4, int i5, int i6, int i7) {
    uint64_t u0 = (uint64_t)i0;
    uint64_t u1 = (uint64_t)i1;
    uint64_t u2 = (uint64_t)i2;
    uint64_t u3 = (uint64_t)i3;
    uint64_t u4 = (uint64_t)i4;
    uint64_t u5 = (uint64_t)i5;
    uint64_t u6 = (uint64_t)i6;
    uint64_t u7 = (uint64_t)i7;
    return (((u0 + u1) * u2 + u3) * u4 + u5) * u6 + u7;
}

int main(int argc, char **argv) {
    {
        // Make an 8-dimensional image
        Buffer<uint64_t> in(2, 3, 4, 5, 6, 7, 8, 9);
        Buffer<uint64_t> out(2, 3, 4, 5, 6, 7, 8, 9);

        // Move the origin
        in.set_min(90, 80, 70, 60, 50, 40, 30, 20);
        out.set_min(90, 80, 70, 60, 50, 40, 30, 20);

        // Fill it in
        for (int i0 = in.dim(0).min(); i0 <= in.dim(0).max(); i0++) {
            for (int i1 = in.dim(1).min(); i1 <= in.dim(1).max(); i1++) {
                for (int i2 = in.dim(2).min(); i2 <= in.dim(2).max(); i2++) {
                    for (int i3 = in.dim(3).min(); i3 <= in.dim(3).max(); i3++) {
                        for (int i4 = in.dim(4).min(); i4 <= in.dim(4).max(); i4++) {
                            for (int i5 = in.dim(5).min(); i5 <= in.dim(5).max(); i5++) {
                                for (int i6 = in.dim(6).min(); i6 <= in.dim(6).max(); i6++) {
                                    for (int i7 = in.dim(7).min(); i7 <= in.dim(7).max(); i7++) {
                                        in(i0, i1, i2, i3, i4, i5, i6, i7) = fn(i0, i1, i2, i3, i4, i5, i6, i7);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Check that for_each_element works with this many dimensions
        int count = 0;
        in.for_each_element([&](int i0, int i1, int i2, int i3, int i4, int i5, int i6, int i7) {
            count++;
            uint64_t correct = fn(i0, i1, i2, i3, i4, i5, i6, i7);
            uint64_t actual = in(i0, i1, i2, i3, i4, i5, i6, i7);
            if (actual != correct) {
                printf("in(%d, %d, %d, %d, %d, %d, %d, %d) = %llu instead of %llu\n",
                       i0, i1, i2, i3, i4, i5, i6, i7, (long long unsigned)actual, (long long unsigned)correct);
                abort();
            }
        });
        if (count != (int)in.number_of_elements()) {
            printf("count = %d instead of %d\n", count, (int)in.number_of_elements());
            return 1;
        }

        // Write Halide code that squares it and subtracts 3
        ImageParam input(UInt(64), 8);
        Func f, g, h;
        Var v0, v1, v2, v3, v4, v5, v6, v7;
        f(v0, v1, v2, v3, v4, v5, v6, v7) = pow(input(v0, v1, v2, v3, v4, v5, v6, v7), 2);
        g(v0, v1, v2, v3, v4, v5, v6, v7) = f(v0, v1, v2, v3, v4, v5, v6, v7) - 2;
        h(_) = g(_) - 1;

        f.compute_root().parallel(v7);
        g.compute_root().parallel(v7);

        input.set(in);
        h.realize(out);

        // Check the results
        for (int i0 = in.dim(0).min(); i0 <= in.dim(0).max(); i0++) {
            for (int i1 = in.dim(1).min(); i1 <= in.dim(1).max(); i1++) {
                for (int i2 = in.dim(2).min(); i2 <= in.dim(2).max(); i2++) {
                    for (int i3 = in.dim(3).min(); i3 <= in.dim(3).max(); i3++) {
                        for (int i4 = in.dim(4).min(); i4 <= in.dim(4).max(); i4++) {
                            for (int i5 = in.dim(5).min(); i5 <= in.dim(5).max(); i5++) {
                                for (int i6 = in.dim(6).min(); i6 <= in.dim(6).max(); i6++) {
                                    for (int i7 = in.dim(7).min(); i7 <= in.dim(7).max(); i7++) {
                                        uint64_t correct = in(i0, i1, i2, i3, i4, i5, i6, i7);
                                        correct *= correct;
                                        correct -= 3;
                                        uint64_t actual = out(i0, i1, i2, i3, i4, i5, i6, i7);
                                        if (actual != correct) {
                                            printf("out(%d, %d, %d, %d, %d, %d, %d, %d) = %llu instead of %llu\n",
                                                   i0, i1, i2, i3, i4, i5, i6, i7,
                                                   (long long unsigned)actual, (long long unsigned)correct);
                                            abort();
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    printf("Success!\n");

    return 0;
}
