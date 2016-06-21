// Halide tutorial lesson 10: AOT compilation part 2

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

    // It starts with declarations for everything in the Halide
    // runtime. The interesting struct is halide_buffer_t, which looks
    // roughly like this:
    //
    // struct halide_buffer_t {
    //     uint64_t device;
    //     const halide_device_interface_t *device_interface;
    //     uint8_t* host;
    //     uint64_t flags;
    //     halide_type_t type;
    //     int32_t dimensions;
    //     halide_dimension_t *dim;
    // };
    //
    // This is how Halide represents input and output images in
    // pre-compiled pipelines. There's a 'host' pointer that points to
    // the start of the image data, the type of the image data, the
    // number of dimensions, and a pointer to an array of
    // halide_dimension_t, which describes the shape of each dimension
    // of the buffer.  There are also some fields related to using the
    // GPU that we'll ignore for now (device, device_interface,
    // flags).

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
    halide_buffer_t input_buf = {0}, output_buf = {0};

    // The host pointers point to the start of the image data:
    input_buf.host  = &input[0];
    output_buf.host = &output[0];

    // Our images will be two-dimensional.
    input_buf.dimensions = output_buf.dimensions = 2;

    // And have type uint8_t
    input_buf.type = output_buf.type = halide_type_of<uint8_t>();

    // We declare arrays for the shape of the input and
    // output. halide_buffer_t can represent buffers of any number of
    // dimensions this way.
    halide_dimension_t in_shape[2], out_shape[2];
    input_buf.dim = in_shape;
    output_buf.dim = out_shape;

    // To access pixel (x, y) in a two-dimensional buffer_t, Halide
    // looks at memory address:

    // host + type.bytes() * ((x - dim[0].min)*dim[0].stride + (y - dim[1].min)*dim[1].stride)

    // The stride in a dimension represents the number of elements in
    // memory between adjacent entries in that dimension. We have a
    // grayscale image stored in scanline order, so dim[1].stride is 1,
    // because pixels that are adjacent in x are next to each other in
    // memory.
    input_buf.dim[0].stride = output_buf.dim[0].stride = 1;

    // dim[1].stride is the width of the image, because pixels that are
    // adjacent in y are separated by a scanline's worth of pixels in
    // memory.
    input_buf.dim[1].stride = output_buf.dim[1].stride = 640;

    // The extent tells us how large the image is in each dimension.
    input_buf.dim[0].extent = output_buf.dim[0].extent = 640;
    input_buf.dim[1].extent = output_buf.dim[1].extent = 480;

    // We'll set the min of each dimension to zero. This is what they
    // typically are. The host pointer points to the memory location
    // of the min coordinate (not the origin!).  See lesson 6 for more
    // detail about the mins.
    input_buf.dim[0].min = output_buf.dim[0].min = 0;
    input_buf.dim[1].min = output_buf.dim[1].min = 0;

    // To avoid repeating all the boilerplate above, We recommend you
    // make a helper function that populates a buffer_t given whatever
    // image type you're using.

    // Now that we've setup our input and output buffers, we can call
    // our function. Looking down the bottom of the the header file,
    // its signature is:

    // int lesson_10_halide(halide_buffer_t *_input_buffer, uint8_t _offset, halide_buffer_t *_brighter_buffer);

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
