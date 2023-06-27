#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support debug_to_file() yet.\n");
        return 0;
    }

    const int size_x = 766;
    const int size_y = 311;

    std::string f_tmp = Internal::get_test_tmp_dir() + "f3.tmp";
    std::string g_tmp = Internal::get_test_tmp_dir() + "g3.tmp";
    std::string h_tmp = Internal::get_test_tmp_dir() + "h3.tmp";

    Internal::ensure_no_file_exists(f_tmp);
    Internal::ensure_no_file_exists(g_tmp);
    Internal::ensure_no_file_exists(h_tmp);

    {
        Func f, g, h, j;
        Var x, y;
        f(x, y) = x + y;
        g(x, y) = cast<float>(f(x, y) + f(x + 1, y));
        h(x, y) = f(x, y) + g(x, y);

        f.compute_root().debug_to_file(f_tmp);
        g.compute_root().debug_to_file(g_tmp);
        h.compute_root().debug_to_file(h_tmp);

        Pipeline p({f, g, h});

        Buffer<int> f_im(size_x + 1, size_y);
        Buffer<float> g_im(size_x, size_y), h_im(size_x, size_y);
        Realization r({f_im, g_im, h_im});
        p.realize(r);
    }

    Internal::assert_file_exists(f_tmp);
    Internal::assert_file_exists(g_tmp);
    Internal::assert_file_exists(h_tmp);

    FILE *f = fopen(f_tmp.c_str(), "rb");
    FILE *g = fopen(g_tmp.c_str(), "rb");
    FILE *h = fopen(h_tmp.c_str(), "rb");
    assert(f && g && h);

    int header[5];
    size_t header_bytes = fread((void *)(&header[0]), 4, 5, f);
    assert(header_bytes == 5);
    assert(header[0] == size_x + 1);
    assert(header[1] == size_y);
    assert(header[2] == 1);
    assert(header[3] == 1);
    assert(header[4] == 7);

    std::vector<int32_t> f_data((size_x + 1) * size_y);
    size_t f_data_bytes = fread((void *)(&f_data[0]), 4, (size_x + 1) * size_y, f);
    assert(f_data_bytes == (size_x + 1) * size_y);
    for (int y = 0; y < size_y; y++) {
        for (int x = 0; x < size_x + 1; x++) {
            int32_t val = f_data[y * (size_x + 1) + x];
            if (val != x + y) {
                printf("f_data[%d, %d] = %d instead of %d\n", x, y, val, x + y);
                return 1;
            }
        }
    }
    fclose(f);

    header_bytes = fread((void *)(&header[0]), 4, 5, g);
    assert(header_bytes == 5);
    assert(header[0] == size_x);
    assert(header[1] == size_y);
    assert(header[2] == 1);
    assert(header[3] == 1);
    assert(header[4] == 0);

    std::vector<float> g_data(size_x * size_y);
    size_t g_data_bytes = fread((void *)(&g_data[0]), 4, size_x * size_y, g);
    assert(g_data_bytes == size_x * size_y);
    for (int y = 0; y < size_y; y++) {
        for (int x = 0; x < size_x; x++) {
            float val = g_data[y * size_x + x];
            float correct = (float)(f_data[y * (size_x + 1) + x] + f_data[y * (size_x + 1) + x + 1]);
            if (val != correct) {
                printf("g_data[%d, %d] = %f instead of %f\n", x, y, val, correct);
                return 1;
            }
        }
    }
    fclose(g);

    header_bytes = fread((void *)(&header[0]), 4, 5, h);
    assert(header_bytes == 5);
    assert(header[0] == size_x);
    assert(header[1] == size_y);
    assert(header[2] == 1);
    assert(header[3] == 1);
    assert(header[4] == 0);

    std::vector<float> h_data(size_x * size_y);
    size_t h_data_bytes = fread((void *)(&h_data[0]), 4, size_x * size_y, h);
    assert(h_data_bytes == size_x * size_y);
    for (int y = 0; y < size_y; y++) {
        for (int x = 0; x < size_x; x++) {
            float val = h_data[y * size_x + x];
            float correct = f_data[y * (size_x + 1) + x] + g_data[y * size_x + x];
            if (val != correct) {
                printf("h_data[%d, %d] = %f instead of %f\n", x, y, val, correct);
                return 1;
            }
        }
    }
    fclose(h);

    printf("Success!\n");
    return 0;
}
