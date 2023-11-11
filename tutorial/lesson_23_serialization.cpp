// Halide tutorial lesson 23: Serialization

// This lesson describes how to serialize pipelines into a binary format 
// which can be saved on disk, and later deserialized and loaded for 
// evaluation.

// Note that you'll need to be using a build of Halide that was configured
// using the WITH_SERIALIZATION=ON macro defined in order for this tutorial
// to work.

// Disclaimer: Serialization is experimental in Halide 17 and is subject to 
// change; we recommend that you avoid relying on it for production work at this time.

// On linux, you can compile this tutorial and run it like so:
// g++ lesson_23*.cpp -g -I <path/to/Halide.h> -I <path/to/tools/halide_image_io.h> -L <path/to/libHalide.so> -lHalide -lpthread -ldl -o lesson_23 -std=c++17
// LD_LIBRARY_PATH=<path/to/libHalide.so> ./lesson_23

// On os x:
// g++ lesson_23*.cpp -g -I <path/to/Halide.h> -I <path/to/tools/halide_image_io.h> -L <path/to/libHalide.so> -lHalide -o lesson_23 -std=c++17
// DYLD_LIBRARY_PATH=<path/to/libHalide.dylib> ./lesson_23

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_23_serialization
// in a shell with the current directory at the top of the halide
// source tree.

#include "Halide.h"
#include <algorithm>
#include <stdio.h>
using namespace Halide;

// Support code for loading pngs.
#include "halide_image_io.h"

int main(int argc, char **argv) {

    // First we'll declare some Vars to use below.
    Var x("x"), y("y"), c("c");

    // Let's start with the same separable blur pipeline that we used in Tutorial 7,
    // with the clamped boundary condition
    {
        // Let's create an ImageParam for an 8-bit RGB image that we'll use for input.
        ImageParam input(UInt(8), 3, "input");

        // Wrap the input in a Func that prevents reading out of bounds:
        Func clamped("clamped");
        Expr clamped_x = clamp(x, 0, input.width() - 1);
        Expr clamped_y = clamp(y, 0, input.height() - 1);
        clamped(x, y, c) = input(clamped_x, clamped_y, c);

        // Upgrade it to 16-bit, so we can do math without it overflowing.
        Func input_16("input_16");
        input_16(x, y, c) = cast<uint16_t>(clamped(x, y, c));

        // Blur it horizontally:
        Func blur_x("blur_x");
        blur_x(x, y, c) = (input_16(x - 1, y, c) +
                           2 * input_16(x, y, c) +
                           input_16(x + 1, y, c)) / 4;

        // Blur it vertically:
        Func blur_y("blur_y");
        blur_y(x, y, c) = (blur_x(x, y - 1, c) +
                           2 * blur_x(x, y, c) +
                           blur_x(x, y + 1, c)) / 4;

        // Convert back to 8-bit.
        Func output("output");
        output(x, y, c) = cast<uint8_t>(blur_y(x, y, c));

        // Now lets serialize the pipeline to disk (must use the .hlpipe file extension)
        Pipeline blur_pipeline(output);
        std::map<std::string, Parameter> params;
        serialize_pipeline(blur_pipeline, "blur.hlpipe", params);

        // The call to serialize_pipeline populates the params map with any input or output parameters
        // that were found ... object's we'll need to attach to buffers if we wish to execute the pipeline
        for(auto named_param: params) {
            std::cout << "Found Param: " << named_param.first << std::endl;
        } 
    }

    // new scope ... everything above is now destroyed! Now lets reconstruct the entire pipeline 
    // from scratch by deserializing it from a file 
    {
        // Lets load a color 8-bit input and connect it to an ImageParam
        Buffer<uint8_t> rgb_image = Halide::Tools::load_image("images/rgb.png");
        ImageParam input(UInt(8), 3, "input");
        input.set(rgb_image); 

        // Now lets populate the params map so we can override the input 
        std::map<std::string, Parameter> params; 
        params.insert({"input", input.parameter()});

        // Lets construct a new pipeline from scratch by deserializing the file we wrote to disk
        Pipeline blur_pipeline = deserialize_pipeline("blur.hlpipe", params);

        // Now realize the pipeline and blur out input image
        Buffer<uint8_t> result = blur_pipeline.realize({rgb_image.width(), rgb_image.height(), 3});

        // Now lets save the result ... we should have another blurry parrot!
        Halide::Tools::save_image(result, "another_blurry_parrot.png");
    }

    // new scope ... everything above is now destroyed!
    {
        // Lets do the same thing again ... construct a new pipeline from scratch by deserializing the file we wrote to disk

        // FIXME: We shouldn't have to populate the params ... but passing an empty map triggers an error in deserialize
        // for a missing input param
        std::map<std::string, Parameter> params; 
        ImageParam input(UInt(8), 3, "input");
        params.insert({"input", input.parameter()});

        // Now deserialize the pipeline from file
        Pipeline blur_pipeline = deserialize_pipeline("blur.hlpipe", params);

        // Now, lets serialize it to an in memory buffer ... rather than writing it to disk
        std::vector<uint8_t> data;
        serialize_pipeline(blur_pipeline, data, params);

        // Now lets deserialize it from memory
        Pipeline deserialized = deserialize_pipeline(data, params);
    }

    printf("Success!\n");
    return 0;
}
