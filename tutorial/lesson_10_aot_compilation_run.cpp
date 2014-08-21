// Before reading this file, see lesson_10_aot_compilation_generate.cpp

// This is the code that actually uses the Halide pipeline we've
// compiled. It does not depend on libHalide, so we won't be including
// Halide.h.
//
// Instead, it depends on the header file that lesson_10_generate
// produced when we ran it:
#include "lesson_10_halide.h"

#include <stdio.h>

int main(int argc, char **argv) {
    // Have a look in the header file above (it won't exist until you've run
    // lesson_10_generate).

    // It starts with a definition of a buffer_t:
    //
    // typedef struct buffer_t {
    //     uint64_t dev;
    //     uint8_t* host;
    //     int32_t extent[4];
    //     int32_t stride[4];
    //     int32_t min[4];
    //     int32_t elem_size;
    //     bool host_dirty;
    //     bool dev_dirty;
    // } buffer_t;
    //
    // This is how Halide represents input and output images in
    // pre-compiled pipelines. There's a 'host' pointer that points to the
    // start of the image data, some fields that describe how to access
    // pixels, and some fields related to using the GPU that we'll ignore
    // for now (dev, host_dirty, dev_dirty).

    // Let's make some input data to test with:
    uint8_t input[640 * 480];
    for (int y = 0; y < 480; y++) {
        for (int x = 0; x < 640; x++) {
            input[y * 640 + x] = x ^ (y + 1);
        }
    }

    // And the memory where we want to write our output:
    uint8_t output[640 * 480];

    // In AOT-compiled mode, Halide doesn't manage this memory for
    // you. You should use whatever image data type makes sense for
    // your application. Halide just needs pointers to it.

    // Now we make a buffer_t to represent our input and output. It's
    // important to zero-initialize them so you don't end up with
    // garbage fields that confuse Halide.
    buffer_t input_buf = {0}, output_buf = {0};

    // The host pointers point to the start of the image data:
    input_buf.host  = &input[0];
    output_buf.host = &output[0];

    // To access pixel (x, y) in a two-dimensional buffer_t, Halide
    // looks at memory address:

    // host + elem_size * ((x - min[0])*stride[0] + (y - min[1])*stride[1])

    // The stride in a dimension represents the number of elements in
    // memory between adjacent entries in that dimension. We have a
    // grayscale image stored in scanline order, so stride[0] is 1,
    // because pixels that are adjacent in x are next to each other in
    // memory.
    input_buf.stride[0] = output_buf.stride[0] = 1;

    // stride[1] is the width of the image, because pixels that are
    // adjacent in y are separated by a scanline's worth of pixels in
    // memory.
    input_buf.stride[1] = output_buf.stride[1] = 640;

    // The extent tells us how large the image is in each dimension.
    input_buf.extent[0] = output_buf.extent[0] = 640;
    input_buf.extent[1] = output_buf.extent[1] = 480;

    // We'll leave the mins as zero. This is what they typically
    // are. The host pointer points to the memory location of the min
    // coordinate (not the origin!).  See lesson 6 for more detail
    // about the mins.

    // The elem_size field tells us how many bytes each element
    // uses. For the 8-bit image we use in this test it's one.
    input_buf.elem_size = output_buf.elem_size = 1;

    // To avoid repeating all the boilerplate above, We recommend you
    // make a helper function that populates a buffer_t given whatever
    // image type you're using.

    // Now that we've setup our input and output buffers, we can call
    // our function. Looking in the header file, it's signature is:

    // int lesson_10_halide(buffer_t *_input, const int32_t _offset, buffer_t *_brighter);

    // The return value is an error code. It's zero on success.

    int offset = 5;
    int error = lesson_10_halide(&input_buf, offset, &output_buf);

    if (error) {
        printf("Halide returned an error: %d\n", error);
        return -1;
    }

    // Now let's check the filter performed as advertised. It was
    // supposed to add the offset to every input pixel.
    for (int y = 0; y < 480; y++) {
        for (int x = 0; x < 640; x++) {
            uint8_t input_val = input[y * 640 + 480];
            uint8_t output_val = output[y * 640 + 480];
            uint8_t correct_val = input_val + offset;
            if (output_val != correct_val) {
                printf("output(%d, %d) was %d instead of %d\n",
                       x, y, output_val, correct_val);
                return -1;
            }
        }
    }

    // Everything worked!
    printf("Success!\n");
    return 0;
}
