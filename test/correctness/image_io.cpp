#include <fstream>

#include "Halide.h"
#include "halide_image_io.h"
#include "test/common/halide_test_dirs.h"

using namespace Halide;

template<typename T>
void test_round_trip(Buffer<T> buf, std::string format) {
    // Save it
    std::ostringstream o;
    o << Internal::get_test_tmp_dir() << "test_" << halide_type_of<T>() << "x" << buf.channels() << "." << format;
    std::string filename = o.str();
    Tools::save_image(buf, filename);

    // TIFF is write-only for now.
    if (format == "tiff") return;

    // Reload it
    Buffer<T> reloaded = Tools::load_image(filename);

    // Ensure that reloaded has the same origin as buf
    for (int d = 0; d < buf.dimensions(); ++d) {
        reloaded.translate(d, buf.dim(d).min() - reloaded.dim(d).min());
    }

    Tools::save_image(reloaded, Internal::get_test_tmp_dir() + "test_reloaded." + format);

    // Check they're not too different.
    RDom r(reloaded);
    std::vector<Expr> args;
    for (int i = 0; i < r.dimensions(); ++i) {
        args.push_back(r[i]);
    }
    uint32_t diff = evaluate<uint32_t>(maximum(abs(cast<int>(buf(args)) - cast<int>(reloaded(args)))));

    uint32_t max_diff = 0;
    if (format == "jpg") {
        max_diff = 32;
    }
    if (diff > max_diff) {
        printf("test_round_trip: Difference of %d when saved and loaded as %s\n", diff, format.c_str());
        abort();
    }
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
    RDom r(buf2);
    std::vector<Expr> args = {r.x, r.y, r.z};
    uint32_t diff = evaluate<uint32_t>(maximum(abs(cast<int>(buf(args)) - cast<int>(buf2(args)))));
    if (diff > 0) {
        printf("test_convert_image_s2s: Difference of %d when converted\n", diff);
        abort();
    }
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
    RDom r(buf2);
    std::vector<Expr> args = {r.x, r.y, r.z};
    uint32_t diff = evaluate<uint32_t>(maximum(abs(cast<int>(buf(args)) - cast<int>(buf2(args)))));
    if (diff > 0) {
        printf("test_convert_image_d2s: Difference of %d when converted\n", diff);
        abort();
    }
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
    RDom r(buf2);
    std::vector<Expr> args = {r.x, r.y, r.z};
    uint32_t diff = evaluate<uint32_t>(maximum(abs(cast<int>(buf(args)) - cast<int>(buf2(args)))));
    if (diff > 0) {
        printf("test_convert_image_s2d: Difference of %d when converted\n", diff);
        abort();
    }
}

// dynamic -> dynamic conversion test
template<typename T>
void test_convert_image_d2d(Buffer<> buf_d) {
    std::cout << "Testing dynamic -> dynamic image conversion for " << halide_type_of<T>() << "\n";

    // convert to float
    Buffer<> buf_float_d = Tools::ImageTypeConversion::convert_image(buf_d, halide_type_t(halide_type_float, 32));

    // convert back to T
    Buffer<> buf2_d = Tools::ImageTypeConversion::convert_image(buf_float_d, halide_type_of<T>());

    // These will do a runtime check
    Buffer<T> buf(buf_d);
    Buffer<T> buf2(buf2_d);

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

template<typename T>
void do_test() {
    const int width = 1600;
    const int height = 1200;

    // Make some colored noise
    Func f;
    Var x, y, c, w;
    const float one = std::numeric_limits<T>::max();
    f(x, y, c) = cast<T>(clamp(make_noise(10)(x, y, c), 0.0f, 1.0f) * one);

    Buffer<T> color_buf = f.realize(width, height, 3);

    // Inset it a bit to ensure that saving buffers with nonzero mins works
    const int inset = 4;
    color_buf.crop(0, inset, width-inset*2);
    color_buf.crop(1, inset, height-inset*2);

    test_convert_image_s2s<T>(color_buf);
    test_convert_image_s2d<T>(color_buf);
    test_convert_image_d2s<T>(color_buf);
    test_convert_image_d2d<T>(color_buf);

    Buffer<T> luma_buf(width, height, 1);
    luma_buf.copy_from(color_buf);
    luma_buf.slice(2);

    std::vector<std::string> formats = {"ppm","pgm","tmp","mat","tiff"};
#ifndef HALIDE_NO_JPEG
    formats.push_back("jpg");
#endif
#ifndef HALIDE_NO_PNG
    formats.push_back("png");
#endif
    for (std::string format : formats) {
        if (format == "jpg" && halide_type_of<T>() != halide_type_t(halide_type_uint, 8)) {
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
            Buffer<T> funky_buf = f2.realize(10, 10, 1, 3);
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

void test_mat_header() {
    // Test if the .mat file header writes the correct file size
    std::ostringstream o;
    Buffer<uint8_t> buf(15, 15);
    buf.fill(42);
    o << Internal::get_test_tmp_dir() << "test_mat_header.mat";
    std::string filename = o.str();
    Tools::save_image(buf, filename);
    std::ifstream fs(filename.c_str(), std::ifstream::binary);
    if (!fs) {
        std::cout << "Cannot read " << filename << std::endl;
        abort();
    }
    fs.seekg(0, fs.end);
    // .mat file begins with a 128 bytes header and a 8 bytes 
    // matrix tag, the second byte of the matrix describe
    // the size of the rest of the file
    uint32_t file_size = uint32_t((int)fs.tellg() - 128 - 8);
    fs.seekg(128 + 4, fs.beg);
    uint32_t stored_file_size = 0;
    fs.read((char*)&stored_file_size, 4);
    fs.close();
    if (file_size != stored_file_size) {
        std::cout << "Wrong file size written for " << filename << ". Expected " <<
            file_size << ", got" << stored_file_size << std::endl;
        abort();
    } 
}

int main(int argc, char **argv) {
    do_test<uint8_t>();
    do_test<uint16_t>();
    test_mat_header();
    return 0;
}
