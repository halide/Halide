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

// static -> static conversion test
void test_convert_image_s2s(Buffer<uint8_t> buf) {
    // convert to float
    Buffer<float> buf_float = Tools::ImageTypeConversion::convert_image<float>(buf);

    // convert back to uint8
    Buffer<uint8_t> buf2 = Tools::ImageTypeConversion::convert_image<uint8_t>(buf_float);

    // Check that they match (this conversion should be exact).
    RDom r(buf2);
    std::vector<Expr> args = {r.x, r.y, r.z};
    uint32_t diff = evaluate<uint32_t>(maximum(abs(cast<int>(buf(args)) - cast<int>(buf2(args)))));
    if (diff > 0) {
        printf("test_convert_static: Difference of %d when converted\n", diff);
        abort();
    }
}

// static -> dynamic conversion test
void test_convert_image_s2d(Buffer<uint8_t> buf) {
    // convert to float
    Buffer<> buf_float_d = Tools::ImageTypeConversion::convert_image(buf, halide_type_t(halide_type_float, 32));
    // This will do a runtime check
    Buffer<float> buf_float(buf_float_d);

    // convert back to uint8
    Buffer<> buf2_d = Tools::ImageTypeConversion::convert_image(buf_float, halide_type_t(halide_type_uint, 8));
    // This will do a runtime check
    Buffer<uint8_t> buf2(buf2_d);

    // Check that they match (this conversion should be exact).
    RDom r(buf2);
    std::vector<Expr> args = {r.x, r.y, r.z};
    uint32_t diff = evaluate<uint32_t>(maximum(abs(cast<int>(buf(args)) - cast<int>(buf2(args)))));
    if (diff > 0) {
        printf("test_convert_image_s2d: Difference of %d when converted\n", diff);
        abort();
    }
}

// dynamic -> dynamic conversion test
void test_convert_image_d2d(Buffer<> buf_d) {
    // convert to float
    Buffer<> buf_float_d = Tools::ImageTypeConversion::convert_image(buf_d, halide_type_t(halide_type_float, 32));

    // convert back to uint8
    Buffer<> buf2_d = Tools::ImageTypeConversion::convert_image(buf_float_d, halide_type_t(halide_type_uint, 8));

    // These will do a runtime check
    Buffer<uint8_t> buf(buf_d);
    Buffer<uint8_t> buf2(buf2_d);

    // Check that they match (this conversion should be exact).
    RDom r(buf2);
    std::vector<Expr> args = {r.x, r.y, r.z};
    uint32_t diff = evaluate<uint32_t>(maximum(abs(cast<int>(buf(args)) - cast<int>(buf2(args)))));
    if (diff > 0) {
        printf("test_convert_image_d2d: Difference of %d when converted\n", diff);
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

    test_convert_image_s2s(color_buf);
    test_convert_image_s2d(color_buf);
    test_convert_image_d2d(color_buf);

    Buffer<uint8_t> luma_buf(width, height, 1);
    luma_buf.copy_from(color_buf);
    luma_buf.slice(2, 0);

    std::vector<std::string> formats = {"ppm"};
#ifndef HALIDE_NO_JPEG
    formats.push_back("jpg");
#endif
#ifndef HALIDE_NO_PNG
    formats.push_back("png");
#endif
    for (std::string format : formats) {
        std::cout << "Testing format: " << format << "\n";
        test_round_trip(color_buf, format);
        if (format != "ppm") {
            // ppm really only supports RGB images.
            test_round_trip(luma_buf, format);
        }
    }
    return 0;
}
