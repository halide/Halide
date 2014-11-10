#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    {
        Func f, g, h, j;
        Var x, y;
        f(x, y) = x + y;
        g(x, y) = cast<float>(f(x, y) + f(x+1, y));
        h(x, y) = f(x, y) + g(x, y);

        Target target = get_jit_target_from_environment();
        if (target.has_gpu_feature()) {
            f.compute_root().gpu_tile(x, y, 1, 1, Device_Default_GPU).debug_to_file("f.tmp");
            g.compute_root().gpu_tile(x, y, 1, 1, Device_Default_GPU).debug_to_file("g.tmp");
            h.compute_root().gpu_tile(x, y, 1, 1, Device_Default_GPU).debug_to_file("h.tmp");
        } else {
            f.compute_root().debug_to_file("f.tmp");
            g.compute_root().debug_to_file("g.tmp");
            h.compute_root().debug_to_file("h.tmp");
        }

        Image<float> im = h.realize(10, 10, target);
    }

    FILE *f = fopen("f.tmp", "rb");
    FILE *g = fopen("g.tmp", "rb");
    FILE *h = fopen("h.tmp", "rb");
    assert(f && g && h);

    int header[5];
    assert(fread((void *)(&header[0]), 4, 5, f) == 5);
    assert(header[0] == 11);
    assert(header[1] == 10);
    assert(header[2] == 1);
    assert(header[3] == 1);
    assert(header[4] == 7);

    int32_t f_data[11*10];
    assert(fread((void *)(&f_data[0]), 4, 11*10, f) == 11*10);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 11; x++) {
            int32_t val = f_data[y*11+x];
            if (val != x+y) {
                printf("f_data[%d, %d] = %d instead of %d\n", x, y, val, x+y);
                return -1;
            }
        }
    }
    fclose(f);

    assert(fread((void *)(&header[0]), 4, 5, g) == 5);
    assert(header[0] == 10);
    assert(header[1] == 10);
    assert(header[2] == 1);
    assert(header[3] == 1);
    assert(header[4] == 0);

    float g_data[10*10];
    assert(fread((void *)(&g_data[0]), 4, 10*10, g) == 10*10);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            float val = g_data[y*10+x];
            float correct = (float)(f_data[y*11+x] + f_data[y*11+x+1]);
            if (val != correct) {
                printf("g_data[%d, %d] = %f instead of %f\n", x, y, val, correct);
                return -1;
            }
        }
    }
    fclose(g);

    assert(fread((void *)(&header[0]), 4, 5, h) == 5);
    assert(header[0] == 10);
    assert(header[1] == 10);
    assert(header[2] == 1);
    assert(header[3] == 1);
    assert(header[4] == 0);

    float h_data[10*10];
    assert(fread((void *)(&h_data[0]), 4, 10*10, h) == 10*10);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            float val = h_data[y*10+x];
            float correct = f_data[y*11+x] + g_data[y*10+x];
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
