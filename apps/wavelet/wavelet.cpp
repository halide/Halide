#include <stdio.h>

#include "daubechies_x.h"
#include "haar_x.h"
#include "inverse_daubechies_x.h"
#include "inverse_haar_x.h"

#include "HalideBuffer.h"
#include "halide_image_io.h"

using namespace Halide::Runtime;
using namespace Halide::Tools;

namespace {

void _assert(bool condition, const char *fmt, ...) {
    if (!condition) {
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
        exit(1);
    }
}

template<typename T>
T clamp(T x, T min, T max) {
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

template<typename T>
void save_untransformed(Buffer<T, 2> t, const std::string &filename) {
    convert_and_save_image(t, filename);
    printf("Saved %s\n", filename.c_str());
}

template<typename T>
void save_transformed(Buffer<T, 3> t, const std::string &filename) {
    Buffer<T, 3> rearranged(t.width() * 2, t.height(), 1);
    for (int y = 0; y < t.height(); y++) {
        for (int x = 0; x < t.width(); x++) {
            rearranged(x, y, 0) = clamp(t(x, y, 0), 0.0f, 1.0f);
            rearranged(x + t.width(), y, 0) = clamp(t(x, y, 1) * 4.f + 0.5f, 0.0f, 1.0f);
        }
    }
    convert_and_save_image(rearranged, filename);
    printf("Saved %s\n", filename.c_str());
}

}  // namespace

int main(int argc, char **argv) {
    _assert(argc == 3, "Usage: main <src_image> <output-dir>\n");

    const std::string src_image = argv[1];
    const std::string dirname = argv[2];

    Buffer<float, 2> input = load_and_convert_image(src_image);
    Buffer<float, 3> transformed(input.width() / 2, input.height(), 2);
    Buffer<float, 2> inverse_transformed(input.width(), input.height());

    _assert(haar_x(input, transformed) == 0, "haar_x failed");
    save_transformed(transformed, dirname + "/haar_x.png");

    _assert(inverse_haar_x(transformed, inverse_transformed) == 0, "inverse_haar_x failed");
    save_untransformed(inverse_transformed, dirname + "/inverse_haar_x.png");

    _assert(daubechies_x(input, transformed) == 0, "daubechies_x failed");
    save_transformed(transformed, dirname + "/daubechies_x.png");

    _assert(inverse_daubechies_x(transformed, inverse_transformed) == 0, "inverse_daubechies_x failed");
    save_untransformed(inverse_transformed, dirname + "/inverse_daubechies_x.png");

    printf("Success!\n");
    return 0;
}
