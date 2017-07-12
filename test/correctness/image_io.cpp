#ifdef _MSC_VER
#include <stdio.h>
int main(int argc, char **argv) {
    printf("Skipping test on Windows\n");
    return 0;
}
#else

#include "Halide.h"
#include "halide_image_io.h"
#include "test/common/halide_test_dirs.h"

using namespace Halide;

void test_round_trip(Buffer<uint8_t> buf, std::string format) {
    // Save it
    std::string filename = Internal::get_test_tmp_dir() + "test." + format;
    Tools::save_image(buf, filename);

    // Reload it
    Buffer<uint8_t> reloaded = Tools::load_image(filename);

    Tools::save_image(reloaded, Internal::get_test_tmp_dir() + "test_reloaded." + format);

    // Check they're not too different.
    RDom r(reloaded);
    std::vector<Expr> args;
    if (buf.dimensions() == 2) {
        args = {r.x, r.y};
    } else {
        args = {r.x, r.y, r.z};
    }
    uint32_t diff = evaluate<uint32_t>(maximum(abs(cast<int>(buf(args)) - cast<int>(reloaded(args)))));

    uint32_t max_diff = 0;
    if (format == "jpg") {
        max_diff = 32;
    }
    if (diff > max_diff) {
        printf("Difference of %d when saved and loaded as %s\n", diff, format.c_str());
        abort();
    }
}

Func make_noise(int depth) {
    Func f;
    Var x, y, c;
    if (depth == 0) {
        f(x, y, c) = random_float();
    } else {
        Func g = make_noise(depth - 1);
        Func g_up;
        f(x, y, c) = (g(x/2, y/2, c) +
                      g((x+1)/2, y/2, c) +
                      g(x/2, (y+1)/2, c) +
                      g((x+1)/2, (y+1)/2, c) +
                      0.25f * random_float()) / 4.25f;
    }
    f.compute_root();
    return f;
}

int main(int argc, char **argv) {
    const int width = 1600;
    const int height = 1200;

    // Make some colored noise
    Func f;
    Var x, y, c;
    f(x, y, c) = cast<uint8_t>(clamp(make_noise(10)(x, y, c), 0.0f, 1.0f) * 255.0f);

    Buffer<uint8_t> color_buf = f.realize(width, height, 3);

    Buffer<uint8_t> luma_buf(width, height, 1);
    luma_buf.copy_from(color_buf);
    luma_buf.slice(2, 0);

    std::string formats[] = {"jpg", "png", "ppm"};
    for (std::string format : formats) {
        test_round_trip(color_buf, format);
        if (format != "ppm") {
            // ppm really only supports RGB images.
            test_round_trip(luma_buf, format);
        }
    }
    return 0;
}

#endif
