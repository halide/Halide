#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    const int size_x = 766;
    const int size_y = 311;

    {
        Func f, g, h, j;
        Var x, y;
        f(x, y) = x + y;
        g(x, y) = cast<float>(f(x, y) + f(x + 1, y));
        h(x, y) = f(x, y) + g(x, y);

        f.compute_root().debug_to_file("f3.tmp");
        g.compute_root().debug_to_file("g3.tmp");
        h.compute_root().debug_to_file("h3.tmp");

        Pipeline p({f, g, h});

        Image<int> f_im(size_x + 1, size_y);
        Image<float> g_im(size_x, size_y), h_im(size_x, size_y);
        Realization r(f_im, g_im, h_im);
        p.realize(r);
    }

    FILE *f = fopen("f3.tmp", "rb");
    FILE *g = fopen("g3.tmp", "rb");
    FILE *h = fopen("h3.tmp", "rb");
    assert(f && g && h);

    int header[5];
    assert(fread((void *)(&header[0]), 4, 5, f) == 5);
    assert(header[0] == size_x+1);
    assert(header[1] == size_y);
    assert(header[2] == 1);
    assert(header[3] == 1);
    assert(header[4] == 7);

    std::vector<int32_t> f_data((size_x + 1)*size_y);
    assert(fread((void *)(&f_data[0]), 4, (size_x+1)*size_y, f) == (size_x+1)*size_y);
    for (int y = 0; y < size_y; y++) {
        for (int x = 0; x < size_x+1; x++) {
            int32_t val = f_data[y*(size_x+1)+x];
            if (val != x+y) {
                printf("f_data[%d, %d] = %d instead of %d\n", x, y, val, x+y);
                return -1;
            }
        }
    }
    fclose(f);

    assert(fread((void *)(&header[0]), 4, 5, g) == 5);
    assert(header[0] == size_x);
    assert(header[1] == size_y);
    assert(header[2] == 1);
    assert(header[3] == 1);
    assert(header[4] == 0);

    std::vector<float> g_data(size_x*size_y);
    assert(fread((void *)(&g_data[0]), 4, size_x*size_y, g) == size_x*size_y);
    for (int y = 0; y < size_y; y++) {
        for (int x = 0; x < size_x; x++) {
            float val = g_data[y*size_x+x];
            float correct = (float)(f_data[y*(size_x+1)+x] + f_data[y*(size_x+1)+x+1]);
            if (val != correct) {
                printf("g_data[%d, %d] = %f instead of %f\n", x, y, val, correct);
                return -1;
            }
        }
    }
    fclose(g);

    assert(fread((void *)(&header[0]), 4, 5, h) == 5);
    assert(header[0] == size_x);
    assert(header[1] == size_y);
    assert(header[2] == 1);
    assert(header[3] == 1);
    assert(header[4] == 0);

    std::vector<float> h_data(size_x*size_y);
    assert(fread((void *)(&h_data[0]), 4, size_x*size_y, h) == size_x*size_y);
    for (int y = 0; y < size_y; y++) {
        for (int x = 0; x < size_x; x++) {
            float val = h_data[y*size_x+x];
            float correct = f_data[y*(size_x+1)+x] + g_data[y*size_x+x];
            if (val != correct) {
                printf("h_data[%d, %d] = %f instead of %f\n", x, y, val, correct);
                return -1;
            }
        }
    }
    fclose(h);

    printf("Success!\n");
    return 0;

}
