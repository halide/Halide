// Halide tutorial lesson 16: RGB images and memory layouts part 1

// This lesson demonstrates how to feed Halide RGB images in
// interleaved or planar format, and how to write code optimized for
// each case.

// On linux or os x, you can compile and run it like so:

// g++ lesson_16_rgb_generate.cpp ../tools/GenGen.cpp -g -std=c++11 -fno-rtti -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_16_generate
// export LD_LIBRARY_PATH=../bin   # For linux
// export DYLD_LIBRARY_PATH=../bin # For OS X
// ./lesson_16_generate -o . -f brighten_planar      target=host layout=planar
// ./lesson_16_generate -o . -f brighten_interleaved target=host layout=interleaved
// ./lesson_16_generate -o . -f brighten_either      target=host layout=either
// ./lesson_16_generate -o . -f brighten_specialized target=host layout=specialized
// g++ lesson_16_rgb_run.cpp brighten_*.o -ldl -lpthread -o lesson_16_run
// ./lesson_16_run

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_16_rgb_run
// in a shell with the current directory at the top of the halide
// source tree.

#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// We will define a generator that brightens an RGB image.
class Brighten : public Halide::Generator<Brighten> {
public:
    // We declare a three-dimensional input image. The first two
    // dimensions will be x, and y, and the third dimension will be
    // the color channel.
    ImageParam input{UInt(8), 3, "input"};

    // We will compile this generator in several ways to accept
    // several different memory layouts for the input and output. This
    // is a good use of a GeneratorParam (see lesson 15).
    enum class Layout { Planar, Interleaved, Either, Specialized };
    GeneratorParam<Layout> layout{"layout",
            // default value
            Layout::Planar,
            // map from names to values
                {{ "planar",        Layout::Planar },
                 { "interleaved",   Layout::Interleaved },
                 { "either",        Layout::Either },
                 { "specialized",   Layout::Specialized }}};

    // We also declare a scalar parameter to control the amount of
    // brightening.
    Param<uint8_t> offset{"offset"};

    // Declare our Vars
    Var x, y, c;

    Func build() {
        // Define the Func.
        Func brighter("brighter");
        brighter(x, y, c) = input(x, y, c) + offset;

        // Schedule it.
        brighter.vectorize(x, 16);

        // We will compile this pipeline to handle memory layouts in
        // several different ways, depending on the 'layout' generator
        // param.
        if (layout == Layout::Planar) {
            // This pipeline as written will only work with images in
            // which each scanline is densely-packed single color
            // channel. In terms of the strides described in lesson
            // 10, Halide assumes and asserts that the stride in x is
            // one.

            // This constraint permits planar images, where the red,
            // green, and blue channels are laid out in memory like
            // this:

            // RRRRRRRR
            // RRRRRRRR
            // RRRRRRRR
            // RRRRRRRR
            // GGGGGGGG
            // GGGGGGGG
            // GGGGGGGG
            // GGGGGGGG
            // BBBBBBBB
            // BBBBBBBB
            // BBBBBBBB
            // BBBBBBBB

            // It also works with the less-commonly used line-by-line
            // layout, in which scanlines of red, green, and blue
            // alternate.

            // RRRRRRRR
            // GGGGGGGG
            // BBBBBBBB
            // RRRRRRRR
            // GGGGGGGG
            // BBBBBBBB
            // RRRRRRRR
            // GGGGGGGG
            // BBBBBBBB
            // RRRRRRRR
            // GGGGGGGG
            // BBBBBBBB

        } else if (layout == Layout::Interleaved) {
            // Another common format is 'interleaved', in which the
            // red, green, and blue values for each pixel occur next
            // to each other in memory:

            // RGBRGBRGBRGBRGBRGBRGBRGB
            // RGBRGBRGBRGBRGBRGBRGBRGB
            // RGBRGBRGBRGBRGBRGBRGBRGB
            // RGBRGBRGBRGBRGBRGBRGBRGB

            // In this case the stride in x is three, the stride in y
            // is three times the width of the image, and the stride
            // in c is one. We can tell Halide to assume (and assert)
            // that this is the case for the input and output like so:

            input
                .set_stride(0, 3) // stride in dimension 0 (x) is three
                .set_stride(2, 1); // stride in dimension 2 (c) is one

            brighter.output_buffer()
                .set_stride(0, 3)
                .set_stride(2, 1);

            // For interleaved layout, you may want to use a different
            // schedule. We'll tell Halide to additionally assume and
            // assert that there are three color channels, then
            // exploit this fact to make the loop over 'c' innermost
            // and unrolled.

            input.set_bounds(2, 0, 3); // Dimension 2 (c) starts at 0 and has extent 3.
            brighter.output_buffer().set_bounds(2, 0, 3);

            // Move the loop over color channels innermost and unroll
            // it.
            brighter.reorder(c, x, y).unroll(c);

            // Note that if we were dealing with an image with an
            // alpha channel (RGBA), then the stride in x and the
            // bounds of the channels dimension would both be four
            // instead of three.

        } else if (layout == Layout::Either) {
            // We can also remove all constraints and compile a
            // pipeline that will work with any memory layout. It will
            // probably be slow, because all vector loads become
            // gathers, and all vector stores become scatters.
            input.set_stride(0, Expr()); // Use a default-constructed
                                         // undefined Expr to mean
                                         // there is no constraint.

            brighter.output_buffer().set_stride(0, Expr());

        } else if (layout == Layout::Specialized) {
            // We can accept any memory layout with good performance
            // by telling Halide to inspect the memory layout at
            // runtime, and branch to different code depending on the
            // strides it find. First we relax the default constraint
            // that stride(0) == 1:

            input.set_stride(0, Expr()); // Use an undefined Expr to
                                         // mean there is no
                                         // constraint.

            brighter.output_buffer().set_stride(0, Expr());

            // The we construct boolean Exprs that detect at runtime
            // whether we're planar or interleaved. The conditions
            // should check for all the facts we want to exploit in
            // each case.
            Expr input_is_planar =
                (input.stride(0) == 1);
            Expr input_is_interleaved =
                (input.stride(0) == 3 &&
                 input.stride(2) == 1 &&
                 input.extent(2) == 3);

            Expr output_is_planar =
                (brighter.output_buffer().stride(0) == 1);
            Expr output_is_interleaved =
                (brighter.output_buffer().stride(0) == 3 &&
                 brighter.output_buffer().stride(2) == 1 &&
                 brighter.output_buffer().extent(2) == 3);

            // We can then use Func::specialize to write a schedule
            // that switches at runtime to specialized code based on a
            // boolean Expr. That code will exploit the fact that the
            // Expr is known to be true.
            brighter.specialize(input_is_planar && output_is_planar);

            // We've already vectorized and parallelized brighter, and
            // our two specializations will inherit those scheduling
            // directives. We can also add additional scheduling
            // directives that apply to a single specialization
            // only. We'll tell Halide to make a specialized version
            // of the code for interleaved layouts, and to reorder and
            // unroll that specialized code.
            brighter.specialize(input_is_interleaved && output_is_interleaved)
                .reorder(c, x, y).unroll(c);

            // We could also add specializations for if the input is
            // interleaved and the output is planar, and vice versa,
            // but two specializations is enough to demonstrate the
            // feature. A later tutorial will explore more creative
            // uses of Func::specialize.

            // Adding specializations can improve performance
            // substantially for the cases they apply to, but it also
            // increases the amount of code to compile and ship. If
            // binary sizes are a concern and the input and output
            // memory layouts are known, you probably want to use
            // set_stride and set_extent instead.
        }

        return brighter;
    }
};

// As in lesson 15, we register our generator and then compile this
// file along with tools/GenGen.cpp.
RegisterGenerator<Brighten> my_first_generator{"brighten"};

// After compiling this file, see how to use it in
// lesson_16_rgb_run.cpp
