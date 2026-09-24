#!/usr/bin/python3

# Halide tutorial lesson 16: RGB images and memory layouts: compiling the
# pipeline

# This lesson demonstrates how to feed Halide RGB images in
# interleaved or planar format, and how to write code optimized for
# each case.

import enum

import halide as hl


class Layout(enum.Enum):
    Planar = 0
    Interleaved = 1
    Either = 2
    Specialized = 3


_LAYOUT_MAP = {
    "planar": Layout.Planar,
    "interleaved": Layout.Interleaved,
    "either": Layout.Either,
    "specialized": Layout.Specialized,
}


# We will define a generator that brightens an RGB image.
@hl.generator(name="brighten")
class Brighten:
    # We declare a three-dimensional input image. The first two
    # dimensions will be x, and y, and the third dimension will be
    # the color channel.
    input_buf = hl.InputBuffer(hl.UInt(8), 3)

    # We will compile this generator in several ways to accept
    # several different memory layouts for the input and output. This
    # is a good use of a GeneratorParam (see lesson 15). Python
    # GeneratorParams don't support enums directly, so we use a string
    # GeneratorParam and map it to our Layout enum ourselves, the same
    # way apps/blur_generator.py does for its gpu_schedule param.
    layout = hl.GeneratorParam("planar")

    # We also declare a scalar input to control the amount of
    # brightening.
    offset = hl.InputScalar(hl.UInt(8))

    # Declare our output
    brighter = hl.OutputBuffer(hl.UInt(8), 3)

    def generate(self):
        g = self
        x, y, c = hl.Var("x"), hl.Var("y"), hl.Var("c")

        # Define the Func.
        g.brighter[x, y, c] = g.input_buf[x, y, c] + g.offset

        # Schedule it.
        g.brighter.vectorize(x, 16)

        # This will raise an exception for unknown strings, which is what we
        # want.
        layout = _LAYOUT_MAP[g.layout]

        # We will compile this pipeline to handle memory layouts in
        # several different ways, depending on the 'layout' generator
        # param.
        if layout == Layout.Planar:
            # This pipeline as written will only work with images in
            # which each scanline is densely-packed single color
            # channel. In terms of the strides described in lesson
            # 10, Halide assumes and asserts that the stride in x is
            # one.

            # This constraint permits planar images, where the red,
            # green, and blue channels are laid out in memory like
            # this:

            # RRRRRRRR
            # RRRRRRRR
            # RRRRRRRR
            # RRRRRRRR
            # GGGGGGGG
            # GGGGGGGG
            # GGGGGGGG
            # GGGGGGGG
            # BBBBBBBB
            # BBBBBBBB
            # BBBBBBBB
            # BBBBBBBB

            # It also works with the less-commonly used line-by-line
            # layout, in which scanlines of red, green, and blue
            # alternate.

            # RRRRRRRR
            # GGGGGGGG
            # BBBBBBBB
            # RRRRRRRR
            # GGGGGGGG
            # BBBBBBBB
            # RRRRRRRR
            # GGGGGGGG
            # BBBBBBBB
            # RRRRRRRR
            # GGGGGGGG
            # BBBBBBBB

            pass

        elif layout == Layout.Interleaved:
            # Another common format is 'interleaved', in which the
            # red, green, and blue values for each pixel occur next
            # to each other in memory:

            # RGBRGBRGBRGBRGBRGBRGBRGB
            # RGBRGBRGBRGBRGBRGBRGBRGB
            # RGBRGBRGBRGBRGBRGBRGBRGB
            # RGBRGBRGBRGBRGBRGBRGBRGB

            # In this case the stride in x is three, the stride in y
            # is three times the width of the image, and the stride
            # in c is one. We can tell Halide to assume (and assert)
            # that this is the case for the input and output like so:

            g.input_buf.dim(0).set_stride(3)  # stride in dimension 0 (x) is three
            g.input_buf.dim(2).set_stride(1)  # stride in dimension 2 (c) is one

            g.brighter.output_buffer().dim(0).set_stride(3)
            g.brighter.output_buffer().dim(2).set_stride(1)

            # For interleaved layout, you may want to use a different
            # schedule. We'll tell Halide to additionally assume and
            # assert that there are three color channels, then
            # exploit this fact to make the loop over 'c' innermost
            # and unrolled.

            g.input_buf.dim(2).set_bounds(
                0, 3
            )  # Dimension 2 (c) starts at 0 and has extent 3.
            g.brighter.output_buffer().dim(2).set_bounds(0, 3)

            # Move the loop over color channels innermost and unroll
            # it.
            g.brighter.reorder(c, x, y).unroll(c)

            # Note that if we were dealing with an image with an
            # alpha channel (RGBA), then the stride in x and the
            # bounds of the channels dimension would both be four
            # instead of three.

        elif layout == Layout.Either:
            # We can also remove all constraints and compile a
            # pipeline that will work with any memory layout. It will
            # probably be slow, because all vector loads become
            # gathers, and all vector stores become scatters.
            g.input_buf.dim(0).set_stride(hl.Expr())  # Use a default-constructed
            # undefined Expr to mean there is no constraint.

            g.brighter.output_buffer().dim(0).set_stride(hl.Expr())

        elif layout == Layout.Specialized:
            # We can accept any memory layout with good performance
            # by telling Halide to inspect the memory layout at
            # runtime, and branch to different code depending on the
            # strides it find. First we relax the default constraint
            # that dim(0).stride() == 1:

            g.input_buf.dim(0).set_stride(hl.Expr())  # Use an undefined Expr to
            # mean there is no constraint.

            g.brighter.output_buffer().dim(0).set_stride(hl.Expr())

            # Then we construct boolean Exprs that detect at runtime
            # whether we're planar or interleaved. The conditions
            # should check for all the facts we want to exploit in
            # each case.
            input_is_planar = g.input_buf.dim(0).stride() == 1
            input_is_interleaved = (
                (g.input_buf.dim(0).stride() == 3)
                & (g.input_buf.dim(2).stride() == 1)
                & (g.input_buf.dim(2).extent() == 3)
            )

            output_is_planar = g.brighter.output_buffer().dim(0).stride() == 1
            output_is_interleaved = (
                (g.brighter.output_buffer().dim(0).stride() == 3)
                & (g.brighter.output_buffer().dim(2).stride() == 1)
                & (g.brighter.output_buffer().dim(2).extent() == 3)
            )

            # We can then use Func.specialize to write a schedule
            # that switches at runtime to specialized code based on a
            # boolean Expr. That code will exploit the fact that the
            # Expr is known to be true.
            g.brighter.specialize(input_is_planar & output_is_planar)

            # We've already vectorized and parallelized brighter, and
            # our two specializations will inherit those scheduling
            # directives. We can also add additional scheduling
            # directives that apply to a single specialization
            # only. We'll tell Halide to make a specialized version
            # of the code for interleaved layouts, and to reorder and
            # unroll that specialized code.
            g.brighter.specialize(input_is_interleaved & output_is_interleaved).reorder(
                c, x, y
            ).unroll(c)

            # We could also add specializations for if the input is
            # interleaved and the output is planar, and vice versa,
            # but two specializations is enough to demonstrate the
            # feature. A later tutorial will explore more creative
            # uses of Func.specialize.

            # Adding specializations can improve performance
            # substantially for the cases they apply to, but it also
            # increases the amount of code to compile and ship. If
            # binary sizes are a concern and the input and output
            # memory layouts are known, you probably want to use
            # set_stride and set_extent instead.


if __name__ == "__main__":
    hl.main()
