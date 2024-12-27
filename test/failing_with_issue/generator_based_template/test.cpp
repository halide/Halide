#include "generated_pipeline.h"
#include <HalideBuffer.h>
#include <cstdio>

int main(int argc, char **argv) {
    Halide::Runtime::Buffer<float, 0> output = Halide::Runtime::Buffer<float>::make_scalar();
    generated_pipeline(output);
    std::printf("Output: %f\n", output());

    return 0;
}
