#include "Halide.h"
// Avoid the need to link this test to libjpeg and libpng
#define HALIDE_NO_JPEG
#define HALIDE_NO_PNG
#include "halide_image_io.h"
#include "halide_test_dirs.h"
#include <gtest/gtest.h>

#include <cstdio>

using namespace Halide;

TEST(DebugToFileTest, SaveAndLoadVariousFormats) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        GTEST_SKIP() << "WebAssembly JIT does not support debug_to_file() yet.";
    }

    std::vector<std::string> formats = {"npy", "mat"};
    for (const auto &format : formats) {
        SCOPED_TRACE("format=" + format);

        std::string f_path = Internal::get_test_tmp_dir() + "f." + format;
        std::string g_path = Internal::get_test_tmp_dir() + "g." + format;
        std::string h_path = Internal::get_test_tmp_dir() + "h." + format;

        Internal::ensure_no_file_exists(f_path);
        Internal::ensure_no_file_exists(g_path);
        Internal::ensure_no_file_exists(h_path);

        {
            Func f, g, h, j;
            Var x, y, z;
            f(x, y, z) = cast<int32_t>(x + y + z);
            g(x, y) = cast<float>(f(x, y, 0) + f(x + 1, y, 1));
            h(x, y) = cast<int32_t>(f(x, y, -1) + g(x, y));

            Target target = get_jit_target_from_environment();
            if (target.has_gpu_feature()) {
                Var xi, yi;
                f.compute_root().gpu_tile(x, y, xi, yi, 1, 1).debug_to_file(f_path);
                g.compute_root().gpu_tile(x, y, xi, yi, 1, 1).debug_to_file(g_path);
                h.compute_root().gpu_tile(x, y, xi, yi, 1, 1).debug_to_file(h_path);
            } else {
                f.compute_root().debug_to_file(f_path);
                g.compute_root().debug_to_file(g_path);
                h.compute_root().debug_to_file(h_path);
            }

            EXPECT_NO_THROW(h.realize({10, 10}, target));
        }

        {
            Internal::assert_file_exists(f_path);
            Internal::assert_file_exists(g_path);
            Internal::assert_file_exists(h_path);

            Buffer<int32_t> f = Tools::load_image(f_path);
            EXPECT_EQ(f.dimensions(), 3);
            EXPECT_EQ(f.dim(0).extent(), 11);
            EXPECT_EQ(f.dim(1).extent(), 10);
            EXPECT_EQ(f.dim(2).extent(), 3);

            for (int z = 0; z < 3; z++) {
                for (int y = 0; y < 10; y++) {
                    for (int x = 0; x < 11; x++) {
                        int32_t val = f(x, y, z);
                        // The min coord gets lost on debug_to_file, so f should be shifted up by one.
                        EXPECT_EQ(val, x + y + z - 1) << "f(" << x << ", " << y << ", " << z << ")";
                    }
                }
            }

            Buffer<float> g = Tools::load_image(g_path);
            EXPECT_EQ(g.dimensions(), 2);
            EXPECT_EQ(g.dim(0).extent(), 10);
            EXPECT_EQ(g.dim(1).extent(), 10);

            for (int y = 0; y < 10; y++) {
                for (int x = 0; x < 10; x++) {
                    float val = g(x, y);
                    float correct = (float)(f(x, y, 1) + f(x + 1, y, 2));
                    EXPECT_EQ(val, correct) << "g(" << x << ", " << y << ")";
                }
            }

            Buffer<int32_t> h = Tools::load_image(h_path);
            EXPECT_EQ(h.dimensions(), 2);
            EXPECT_EQ(h.dim(0).extent(), 10);
            EXPECT_EQ(h.dim(1).extent(), 10);

            for (int y = 0; y < 10; y++) {
                for (int x = 0; x < 10; x++) {
                    int32_t val = h(x, y);
                    int32_t correct = f(x, y, 0) + g(x, y);
                    EXPECT_EQ(val, correct) << "h(" << x << ", " << y << ")";
                }
            }
        }
    }
}
