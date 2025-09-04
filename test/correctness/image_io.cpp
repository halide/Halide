#include "Halide.h"
#include <gtest/gtest.h>

#include "halide_image_io.h"
#include "halide_test_dirs.h"

#include <fstream>

using namespace Halide;

// TODO: hack - https://github.com/google/googletest/issues/4369
namespace testing::internal {
template<>
std::string GetTypeName<_Float16>() {
    return "_Float16";
}
}  // namespace testing::internal

namespace {
template<typename T>
void test_round_trip(Buffer<T> buf, const std::string &format) {
    // Save it
    std::ostringstream o;
    o << Internal::get_test_tmp_dir() << "test_" << halide_type_of<T>() << "x" << buf.channels() << "." << format;
    std::string filename = o.str();
    Tools::save_image(buf, filename);

    // TIFF is write-only for now.
    if (format == "tiff") {
        return;
    }

    // Reload it
    Buffer<T> reloaded = Tools::load_image(filename);

    // Ensure that reloaded has the same origin as buf
    for (int d = 0; d < buf.dimensions(); ++d) {
        reloaded.translate(d, buf.dim(d).min() - reloaded.dim(d).min());
    }

    o = std::ostringstream();
    o << Internal::get_test_tmp_dir() << "test_" << halide_type_of<T>() << "x" << buf.channels() << ".reloaded." << format;
    filename = o.str();
    Tools::save_image(reloaded, filename);

    // Check they're not too different.
    double tolerance = format == "jpg" ? 32 : 1e-5;
    reloaded.for_each_element([&](const int *coord) {
        T reloaded_val = reloaded(coord);
        T buf_val = buf(coord);
        if (!std::isnan(reloaded_val) || !std::isnan(buf_val)) {
            EXPECT_NEAR(reloaded(coord), buf(coord), tolerance);
        }
    });
}

// static -> static conversion test
template<typename T>
void test_convert_image_s2s(Buffer<T> buf) {
    std::cout << "Testing static -> static image conversion for " << halide_type_of<T>() << "\n";

    // convert to float
    Buffer<float> buf_float = Tools::ImageTypeConversion::convert_image<float>(buf);

    // convert back to T
    Buffer<T> buf2 = Tools::ImageTypeConversion::convert_image<T>(buf_float);

    // Check that they match (this conversion should be exact).
    buf2.for_each_element([&](int x, int y, int z) {
        EXPECT_EQ(buf2(x, y, z), buf(x, y, z));
    });
}

// dynamic -> static conversion test
template<typename T>
void test_convert_image_d2s(Buffer<T> buf) {
    std::cout << "Testing dynamic -> static image conversion for " << halide_type_of<T>() << "\n";

    // convert to float
    Buffer<> buf_d(buf);
    Buffer<float> buf_float = Tools::ImageTypeConversion::convert_image<float>(buf_d);

    // convert back to T
    Buffer<> buf_float_d(buf_float);
    Buffer<T> buf2 = Tools::ImageTypeConversion::convert_image<T>(buf_float_d);

    // Check that they match (this conversion should be exact).
    buf2.for_each_element([&](int x, int y, int z) {
        EXPECT_EQ(buf2(x, y, z), buf(x, y, z));
    });
}

// static -> dynamic conversion test
template<typename T>
void test_convert_image_s2d(Buffer<T> buf) {
    std::cout << "Testing static -> dynamic image conversion for " << halide_type_of<T>() << "\n";

    // convert to float
    Buffer<> buf_float_d = Tools::ImageTypeConversion::convert_image(buf, halide_type_t(halide_type_float, 32));
    // This will do a runtime check
    Buffer<float> buf_float(buf_float_d);

    // convert back to T
    Buffer<> buf2_d = Tools::ImageTypeConversion::convert_image(buf_float, halide_type_of<T>());
    // This will do a runtime check
    Buffer<T> buf2(buf2_d);

    // Check that they match (this conversion should be exact).
    buf2.for_each_element([&](int x, int y, int z) {
        EXPECT_EQ(buf2(x, y, z), buf(x, y, z));
    });
}

// dynamic -> dynamic conversion test
template<typename T>
void test_convert_image_d2d(const Buffer<>& buf_d) {
    std::cout << "Testing dynamic -> dynamic image conversion for " << halide_type_of<T>() << "\n";

    // convert to float
    Buffer<> buf_float_d = Tools::ImageTypeConversion::convert_image(buf_d, halide_type_t(halide_type_float, 32));

    // convert back to T
    Buffer<> buf2_d = Tools::ImageTypeConversion::convert_image(buf_float_d, halide_type_of<T>());

    // These will do a runtime check
    Buffer<T> buf(buf_d);
    Buffer<T> buf2(buf2_d);

    // Check that they match (this conversion should be exact).
    buf2.for_each_element([&](int x, int y, int z) {
        EXPECT_EQ(buf2(x, y, z), buf(x, y, z));
    });
}

Func make_noise(int depth) {
    Func f;
    Var x, y, c;
    if (depth == 0) {
        f(x, y, c) = random_float();
    } else {
        Func g = make_noise(depth - 1);
        Func g_up;
        f(x, y, c) = (g(x / 2, y / 2, c) +
                      g((x + 1) / 2, y / 2, c) +
                      g(x / 2, (y + 1) / 2, c) +
                      g((x + 1) / 2, (y + 1) / 2, c) +
                      0.25f * random_float()) /
                     4.25f;
    }
    f.compute_root();
    return f;
}
}  // namespace

template<typename T>
class ImageIOTest : public ::testing::Test {};

using ImageIOTypes = ::testing::Types<
    int8_t, int16_t, int32_t, int64_t,
    uint8_t, uint16_t, uint32_t, uint64_t,
    float, double
#ifdef HALIDE_CPP_COMPILER_HAS_FLOAT16
    ,
    _Float16
#endif
    >;

TYPED_TEST_SUITE(ImageIOTest, ImageIOTypes);

TYPED_TEST(ImageIOTest, RoundTrip) {
    using T = TypeParam;

    const int width = 160;
    const int height = 120;

    // Make some colored noise
    Func f;
    Var x, y, c, w;
    const Expr one = std::is_floating_point<T>::value ? Expr(1.0) : Expr(std::numeric_limits<T>::max());
    f(x, y, c) = cast<T>(clamp(make_noise(10)(x, y, c), Expr(0.0), Expr(1.0)) * one);

    Buffer<T> color_buf = f.realize({width, height, 3});

    // Inset it a bit to ensure that saving buffers with nonzero mins works
    const int inset = 4;
    color_buf.crop(0, inset, width - inset * 2);
    color_buf.crop(1, inset, height - inset * 2);

    const auto ht = halide_type_of<T>();
    if (ht == halide_type_t(halide_type_uint, 8) || ht == halide_type_t(halide_type_uint, 16)) {
        test_convert_image_s2s<T>(color_buf);
        test_convert_image_s2d<T>(color_buf);
        test_convert_image_d2s<T>(color_buf);
        test_convert_image_d2d<T>(color_buf);
    }

    Buffer<T> luma_buf(width, height, 1);
    luma_buf.copy_from(color_buf);
    luma_buf.slice(2);

    std::vector<std::string> formats = {"npy", "ppm", "pgm", "tmp", "mat", "tiff"};
#ifndef HALIDE_NO_JPEG
    formats.emplace_back("jpg");
#endif
#ifndef HALIDE_NO_PNG
    formats.emplace_back("png");
#endif
    for (const std::string &format : formats) {
        // .npy is the only format here that supports float16
        if (halide_type_of<T>() == halide_type_t(halide_type_float, 16) && format != "npy") {
            continue;
        }
        if ((format == "jpg" || format == "pgm" || format == "ppm") && ht != halide_type_t(halide_type_uint, 8)) {
            continue;
        }
        if (format == "png" && ht != halide_type_t(halide_type_uint, 8) && ht != halide_type_t(halide_type_uint, 16)) {
            continue;
        }
        if (format == "tmp") {
            // .tmp only supports exactly-4-dimensions, so handle it separately.
            // (Add a dimension to make it 4-dimensional)
            Buffer<T> cb4 = color_buf.embedded(color_buf.dimensions());
            std::cout << "Testing format: " << format << " for " << halide_type_of<T>() << "x4\n";
            test_round_trip(cb4, format);

            // Here we test matching strides
            Func f2;
            f2(x, y, c, w) = f(x, y, c);
            Buffer<T> funky_buf = f2.realize({10, 10, 1, 3});
            funky_buf.fill(42);

            std::cout << "Testing format: " << format << " for " << halide_type_of<T>() << "x4\n";
            test_round_trip(funky_buf, format);

            continue;
        }
        if (format != "pgm") {
            std::cout << "Testing format: " << format << " for " << halide_type_of<T>() << "x3\n";
            // pgm really only supports gray images.
            test_round_trip(color_buf, format);
        }
        if (format != "ppm") {
            std::cout << "Testing format: " << format << " for " << halide_type_of<T>() << "x1\n";
            // ppm really only supports RGB images.
            test_round_trip(luma_buf, format);
        }
    }
}

TEST(ImageIOTest, MatHeader) {
    // Test if the .mat file header writes the correct file size
    const std::string filename = Internal::get_test_tmp_dir() + "test_mat_header.mat";
    Buffer<uint8_t> buf(15, 15);
    buf.fill(42);
    Tools::save_image(buf, filename);
    std::ifstream fs(filename.c_str(), std::ifstream::binary);
    ASSERT_TRUE(fs) << "Cannot read " << filename;
    fs.seekg(0, std::ifstream::end);
    // .mat file begins with a 128 bytes header and a 8 bytes
    // matrix tag, the second byte of the matrix describe
    // the size of the rest of the file
    uint32_t file_size = uint32_t((int)fs.tellg() - 128 - 8);
    fs.seekg(128 + 4, std::ifstream::beg);
    uint32_t stored_file_size = 0;
    fs.read((char *)&stored_file_size, 4);
    fs.close();
    EXPECT_EQ(file_size, stored_file_size) << "Wrong file size written for " << filename;
}
