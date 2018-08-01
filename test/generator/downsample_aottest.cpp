#include "downsample.h"
#include "HalideBuffer.h"

using namespace Halide::Runtime;

int main(int argc, char **argv) {
    const int w = 4;
    const int h = 1;
    Buffer<uint8_t> input(w, h), output_cpu(w, h);
    const uint8_t data[4] = { 172, 47, 117, 192 };
    input.for_each_element([&](int x, int y) { input(x, y) = data[x]; });

    downsample(input.copy(), output_cpu);
    output_cpu.copy_to_host();

    output_cpu.for_each_element([&](int x, int y) {
            printf("%d %d %d\n", x, y, output_cpu(x, y));
        });

    return 0;
}
