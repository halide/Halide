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
        j(x, y) = h(x, y) * 2;

        f.compute_root().debug_to_file("f.tmp");
        g.compute_root().debug_to_file("g.tmp");        
        h.compute_root();

        Image<float> im = j.realize(10, 10);
    }

    FILE *f = fopen("f.tmp", "rb"), *g = fopen("g.tmp", "rb");
    assert(f && g);

    int header[5];
    fread((void *)(&header[0]), 4, 5, f);
    assert(header[0] == 11);
    assert(header[1] == 10);
    assert(header[2] == 1);
    assert(header[3] == 1);
    assert(header[4] == 7);
    
    int32_t f_data[11*10];
    fread((void *)(&f_data[0]), 4, 11*10, f);
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

    fread((void *)(&header[0]), 4, 5, g);
    assert(header[0] == 10);
    assert(header[1] == 10);
    assert(header[2] == 1);
    assert(header[3] == 1);
    assert(header[4] == 0);
    
    float g_data[10*10];
    fread((void *)(&g_data[0]), 4, 10*10, g);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 11; x++) {
            int32_t val = f_data[y*11+x];
            if (val != x+y) {
                printf("f_data[%d, %d] = %d instead of %d\n", x, y, val, x+y);
                return -1;
            }
        }
    }
    fclose(g);

    printf("Success!\n");
    return 0;

}
