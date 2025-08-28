#include "Halide.h"
#include "halide_test_dirs.h"
#include <gtest/gtest.h>

#include <cstdio>

using namespace Halide;

TEST(DebugToFileTest, Reorder) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        GTEST_SKIP() << "WebAssembly JIT does not support debug_to_file() yet.";
    }

    const int size_x = 766;
    const int size_y = 311;

    std::string f_tmp = Internal::get_test_tmp_dir() + "f2.tmp";
    std::string g_tmp = Internal::get_test_tmp_dir() + "g2.tmp";
    std::string h_tmp = Internal::get_test_tmp_dir() + "h2.tmp";

    Internal::ensure_no_file_exists(f_tmp);
    Internal::ensure_no_file_exists(g_tmp);
    Internal::ensure_no_file_exists(h_tmp);

    {
        Func f, g, h, j;
        Var x, y;
        f(x, y) = x + y;
        g(x, y) = cast<float>(f(x, y) + f(x + 1, y));
        h(x, y) = f(x, y) + g(x, y);

        Target target = get_jit_target_from_environment();
        if (target.has_gpu_feature()) {
            Var xi, yi;
            f.compute_root().gpu_tile(x, y, xi, yi, 1, 1).reorder_storage(y, x).debug_to_file(f_tmp);
            g.compute_root().gpu_tile(x, y, xi, yi, 1, 1).reorder_storage(y, x).debug_to_file(g_tmp);
            h.compute_root().gpu_tile(x, y, xi, yi, 1, 1).debug_to_file(h_tmp);
        } else {
            f.compute_root().reorder_storage(y, x).debug_to_file(f_tmp);
            g.compute_root().reorder_storage(y, x).debug_to_file(g_tmp);
            h.compute_root().debug_to_file(h_tmp);
        }

        EXPECT_NO_THROW(h.realize({size_x, size_y}, target));
    }

    Internal::assert_file_exists(f_tmp);
    Internal::assert_file_exists(g_tmp);
    Internal::assert_file_exists(h_tmp);

    FILE *f = fopen(f_tmp.c_str(), "rb");
    FILE *g = fopen(g_tmp.c_str(), "rb");
    FILE *h = fopen(h_tmp.c_str(), "rb");
    ASSERT_TRUE(f != nullptr);
    ASSERT_TRUE(g != nullptr);
    ASSERT_TRUE(h != nullptr);

    int header[5];
    size_t header_bytes = fread((void *)(&header[0]), 4, 5, f);
    ASSERT_EQ(header_bytes, 5u);
    EXPECT_EQ(header[0], size_x + 1);
    EXPECT_EQ(header[1], size_y);
    EXPECT_EQ(header[2], 1);
    EXPECT_EQ(header[3], 1);
    EXPECT_EQ(header[4], 7);

    std::vector<int32_t> f_data((size_x + 1) * size_y);
    size_t f_data_bytes = fread((void *)(&f_data[0]), 4, (size_x + 1) * size_y, f);
    ASSERT_EQ(f_data_bytes, static_cast<size_t>((size_x + 1) * size_y));
    for (int y = 0; y < size_y; y++) {
        for (int x = 0; x < size_x + 1; x++) {
            int32_t val = f_data[y * (size_x + 1) + x];
            EXPECT_EQ(val, x + y) << "f_data[" << x << ", " << y << "]";
        }
    }
    fclose(f);

    header_bytes = fread((void *)(&header[0]), 4, 5, g);
    ASSERT_EQ(header_bytes, 5u);
    EXPECT_EQ(header[0], size_x);
    EXPECT_EQ(header[1], size_y);
    EXPECT_EQ(header[2], 1);
    EXPECT_EQ(header[3], 1);
    EXPECT_EQ(header[4], 0);

    std::vector<float> g_data(size_x * size_y);
    size_t g_data_bytes = fread((void *)(&g_data[0]), 4, size_x * size_y, g);
    ASSERT_EQ(g_data_bytes, static_cast<size_t>(size_x * size_y));
    for (int y = 0; y < size_y; y++) {
        for (int x = 0; x < size_x; x++) {
            float val = g_data[y * size_x + x];
            float correct = (float)(f_data[y * (size_x + 1) + x] + f_data[y * (size_x + 1) + x + 1]);
            EXPECT_EQ(val, correct) << "g_data[" << x << ", " << y << "]";
        }
    }
    fclose(g);

    header_bytes = fread((void *)(&header[0]), 4, 5, h);
    ASSERT_EQ(header_bytes, 5u);
    EXPECT_EQ(header[0], size_x);
    EXPECT_EQ(header[1], size_y);
    EXPECT_EQ(header[2], 1);
    EXPECT_EQ(header[3], 1);
    EXPECT_EQ(header[4], 0);

    std::vector<float> h_data(size_x * size_y);
    size_t h_data_bytes = fread((void *)(&h_data[0]), 4, size_x * size_y, h);
    ASSERT_EQ(h_data_bytes, static_cast<size_t>(size_x * size_y));
    for (int y = 0; y < size_y; y++) {
        for (int x = 0; x < size_x; x++) {
            float val = h_data[y * size_x + x];
            float correct = f_data[y * (size_x + 1) + x] + g_data[y * size_x + x];
            EXPECT_EQ(val, correct) << "h_data[" << x << ", " << y << "]";
        }
    }
    fclose(h);
}
