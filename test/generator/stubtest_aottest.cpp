#include <algorithm>

#include "HalideRuntime.h"
#include "HalideBuffer.h"
#include "stubtest.h"

using Halide::Buffer;

const int kSize = 32;

template<typename Type>
Buffer<Type> make_image(int extra) {
    Buffer<Type> im(kSize, kSize, 3);
    for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
            for (int c = 0; c < 3; c++) {
                im(x, y, c) = static_cast<Type>(x + y + c + extra);
            }
        }
    }
    return im;
}

template<typename InputType, typename OutputType>
void verify(const Buffer<InputType> &input, float float_arg, int int_arg, const Buffer<OutputType> &output) {
    if (input.width() != output.width() ||
        input.height() != output.height()) {
        fprintf(stderr, "size mismatch: %dx%d vs %dx%d\n",input.width(),input.height(),output.width(),output.height());
        exit(-1);
    }
    int channels = std::max(1, std::min(input.channels(), output.channels()));
    for (int x = 0; x < output.width(); x++) {
        for (int y = 0; y < output.height(); y++) {
            for (int c = 0; c < channels; c++) {
                const OutputType expected = static_cast<OutputType>(input(x, y, c) * float_arg + int_arg);
                const OutputType actual = output(x, y, c);
                if (expected != actual) {
                    fprintf(stderr, "img[%d, %d, %d] = %f, expected %f (input = %f)\n", x, y, c, (double)actual, (double)expected, (double)input(x, y, c));
                    exit(-1);
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    Buffer<float> in0 = make_image<float>(0);
    Buffer<float> in1 = make_image<float>(1);
    Buffer<float> f0(kSize, kSize, 3), f1(kSize, kSize, 3);
    Buffer<int16_t> g0(kSize, kSize), g1(kSize, kSize);

    stubtest(in0, in1, 1.25f, 33, 66, f0, f1, g0, g1);

    verify(in0, 1.25f, 0, f0);
    verify(in0, 1.25f, 33, f1);
    verify(in0, 1.0f, 33, g0);
    verify(in1, 1.0f, 66, g1);

    printf("Success!\n");
    return 0;
}
