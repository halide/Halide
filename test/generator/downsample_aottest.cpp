#include "downsample.h"
#include "HalideBuffer.h"

using namespace Halide::Runtime;

int main(int argc, char **argv) {
    const int w = 4;
    const int h = 1;
    Buffer<uint8_t> input(w, h);
    Buffer<uint16_t> output(w, h);
    const uint8_t data[4] = { 2, 3, 5, 7 };
    input.for_each_element([&](int x, int y) { input(x, y) = data[x]; });

    downsample(input.copy(), output);
    output.copy_to_host();

    output.for_each_element([&](int x, int y) {
        printf("%d %d %d\n", x, y, output(x, y));
    });

    return 0;
}
