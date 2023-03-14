#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func first, second;
    Var x, y, yi;

    ImageParam tmp(Int(32), 2);

    // Define two independent pipelines.
    first(x, y) = x + y;

    // The second depends on an ImageParam
    second(x, y) = tmp(x - 1, y - 1) + tmp(x + 3, y + 1);

    // This would fail, because tmp isn't attached to an allocated buffer.
    // Buffer<int> out = second.realize({1024, 1024});

    // Allocate an output image.
    Buffer<int> out(1024, 1024);

    // Ask second to allocate its inputs for us.
    second.infer_input_bounds(out);

    // Check the buffer was allocated and is of the expected size.
    Buffer<int> b = tmp.get();
    assert(b.data());
    assert(b.extent(0) == 1028);
    assert(b.extent(1) == 1026);

    // Now fill the intermediate using the first pipeline, and then
    // run the second pipeline.
    first.realize(b);
    second.realize(out);

    // Make another version of the same thing that isn't split into two to compare.
    Func first_and_second;
    first_and_second(x, y) = first(x - 1, y - 1) + first(x + 3, y + 1);

    Buffer<int> reference = first_and_second.realize({1024, 1024});

    for (int y = 0; y < 1024; y++) {
        for (int x = 0; x < 1024; x++) {
            if (out(x, y) != reference(x, y)) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), reference(x, y));
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
