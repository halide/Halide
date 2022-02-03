#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "g2.h"
#include "g2_lambda.h"
#include "g2_pipeline.h"
#include "g2_tuple.h"

using namespace Halide::Runtime;

const int kSize = 4;

void verify(const Buffer<int32_t> &img, float compiletime_factor, float runtime_factor, int channels) {
    img.for_each_element([=](int x, int y, int c) {
        int expected = (int32_t)(compiletime_factor * runtime_factor * c * (x > y ? x : y));
        int actual = img(x, y, c);
        assert(expected == actual);
    });
}

int main(int argc, char **argv) {

    Buffer<int32_t> input(kSize, kSize);
    const int32_t offset = 32;

    input.for_each_element([&](int x, int y) {
        input(x, y) = (x + y);
    });

    {
        Buffer<int32_t> output(kSize, kSize);
        g2(input, offset, output);
        const int32_t scaling = 2;  // GeneratorParam, aka "Constant"
        output.for_each_element([&](int x, int y) {
            int expected = input(x, y) * scaling + offset;
            int actual = output(x, y);
            if (expected != actual) {
                fprintf(stderr, "g2: at %d %d, expected %d, actual %d\n", x, y, expected, actual);
                exit(-1);
            }
        });
    }

    {
        Buffer<int32_t> output(kSize, kSize);
        g2_lambda(input, offset, output);
        const int32_t scaling = 33;  // GeneratorParam, aka "Constant"
        output.for_each_element([&](int x, int y) {
            int expected = input(x, y) * scaling + offset;
            int actual = output(x, y);
            if (expected != actual) {
                fprintf(stderr, "g2_lambda: at %d %d, expected %d, actual %d\n", x, y, expected, actual);
                exit(-1);
            }
        });
    }

    {
        Buffer<double> finput(kSize, kSize);
        finput.for_each_element([&](int x, int y) {
            input(x, y) = (x + y + 1.5);
        });

        Buffer<int32_t> output(kSize, kSize);
        Buffer<double> foutput(kSize, kSize);
        const double foffset = offset + 1;
        g2_tuple(input, finput, offset, foffset, output, foutput);
        const int32_t scaling = 2;  // GeneratorParam, aka "Constant"
        output.for_each_element([&](int x, int y) {
            int expected = input(x, y) * scaling + offset;
            int actual = output(x, y);
            if (expected != actual) {
                fprintf(stderr, "g2_tuple[1]: at %d %d, expected %d, actual %d\n", x, y, expected, actual);
                exit(-1);
            }
        });
        foutput.for_each_element([&](int x, int y) {
            const double fscaling = 0.5 * scaling;
            double fexpected = finput(x, y) * fscaling + foffset;
            double factual = foutput(x, y);
            if (fexpected != factual) {
                fprintf(stderr, "g2_tuple[2]: at %d %d, expected %f, actual %f\n", x, y, fexpected, factual);
                exit(-1);
            }
        });
    }

    {
        Buffer<int32_t> output0(kSize, kSize);
        Buffer<int32_t> output1(kSize * 2, kSize * 2);
        g2_pipeline(input, offset, output0, output1);
        const int32_t scaling = 2;  // GeneratorParam, aka "Constant"
        output0.for_each_element([&](int x, int y) {
            int expected = input(x, y) * scaling + offset;
            int actual = output0(x, y);
            if (expected != actual) {
                fprintf(stderr, "g2_pipeline[0]: at %d %d, expected %d, actual %d\n", x, y, expected, actual);
                exit(-1);
            }
        });
        output1.for_each_element([&](int x, int y) {
            int expected = input(x / 2, y / 2) * scaling + offset;
            int actual = output1(x, y);
            if (expected != actual) {
                fprintf(stderr, "g2_pipeline[1]: at %d %d, expected %d, actual %d\n", x, y, expected, actual);
                exit(-1);
            }
        });
    }

    printf("Success!\n");
    return 0;
}
