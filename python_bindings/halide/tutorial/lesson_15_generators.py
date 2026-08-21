#!/usr/bin/python3

# Halide tutorial lesson 15: Generators: writing a generator

# This lesson demonstrates how to encapsulate Halide pipelines into
# reusable components called generators.

import enum

import halide as hl


# Generators are a more structured way to do ahead-of-time
# compilation of Halide pipelines. Instead of writing a main()
# with an ad-hoc command-line interface like we did in lesson 10, we
# define a class decorated with @hl.generator().
@hl.generator(name="my_first_generator")
class MyFirstGenerator:
    # We declare the Inputs to the Halide pipeline as class-level
    # attributes. They'll appear in the signature of our generated
    # function in the same order as we declare them.
    offset = hl.InputScalar(hl.UInt(8))
    input_buf = hl.InputBuffer(hl.UInt(8), 2)

    # We also declare the Output as a class-level attribute.
    brighter = hl.OutputBuffer(hl.UInt(8), 2)

    # We then define a method that constructs the Halide pipeline:
    def generate(self):
        g = self

        # Typically you declare your Vars at the top of this method, so
        # that they can be used in any helper methods you add later.
        x, y = hl.Var("x"), hl.Var("y")

        # In lesson 10, here is where we called
        # Func.compile_to_file. In a Generator, we just need to
        # define the Output(s) representing the output of the pipeline.
        g.brighter[x, y] = g.input_buf[x, y] + g.offset

        # Schedule it.
        g.brighter.vectorize(x, 16).parallel(y)


# If you like, you can put multiple Generators in the one file. This
# could be a good idea if they share some common code. Let's define
# another more complex generator:


# This generator will take some compile-time parameters too. These let
# you compile multiple variants of a Halide pipeline. We'll define one
# that tells us whether or not to parallelize in our schedule, and
# another representing a constant scale factor to use. You can define
# GeneratorParams of all the basic scalar types (str, bool, int,
# float). For numeric types, Halide's Generator API lets you
# optionally provide a minimum and maximum value; the Python bindings
# don't support that directly, so we just document the expected range
# in a comment instead.
#
# Python's GeneratorParam also doesn't support enums directly, so to
# make an enum-like GeneratorParam work, we provide a mapping from
# strings to our enum values ourselves, the same way
# apps/blur_generator.py does for its gpu_schedule param.
class Rotation(enum.Enum):
    NONE = 0
    CLOCKWISE = 1
    COUNTER_CLOCKWISE = 2


_ROTATION_MAP = {
    "none": Rotation.NONE,
    "cw": Rotation.CLOCKWISE,
    "ccw": Rotation.COUNTER_CLOCKWISE,
}


@hl.generator(name="my_second_generator")
class MySecondGenerator:
    parallel = hl.GeneratorParam(True)  # default value

    # minimum value 0.0, maximum value 100.0
    scale = hl.GeneratorParam(1.0)  # default value

    rotation = hl.GeneratorParam("none")  # default value; "none", "cw", or "ccw"

    # We'll use the same Inputs as before:
    offset = hl.InputScalar(hl.UInt(8))
    input_buf = hl.InputBuffer(hl.UInt(8), 2)

    # And a similar Output. Note that we don't specify a type for the
    # Buffer: at compile-time, we must specify an explicit type via the
    # "output.type" GeneratorParam (which is implicitly defined for this
    # Output).
    output = hl.OutputBuffer(None, 2)

    def generate(self):
        g = self
        x, y = hl.Var("x"), hl.Var("y")

        # Define the Func. We'll use the compile-time scale factor as
        # well as the runtime offset param.
        brighter = hl.Func("brighter")
        brighter[x, y] = g.scale * (g.input_buf[x, y] + g.offset)

        # We'll possibly do some sort of rotation, depending on the
        # enum. This will raise an exception for unknown strings, which
        # is what we want.
        rotation = _ROTATION_MAP[g.rotation]

        rotated = hl.Func("rotated")
        if rotation == Rotation.NONE:
            rotated[x, y] = brighter[x, y]
        elif rotation == Rotation.CLOCKWISE:
            rotated[x, y] = brighter[y, 100 - x]
        elif rotation == Rotation.COUNTER_CLOCKWISE:
            rotated[x, y] = brighter[100 - y, x]

        # We'll then cast to the desired output type.
        g.output[x, y] = hl.cast(g.output.type(), rotated[x, y])

        # The structure of the pipeline depended on the generator
        # params. So will the schedule.

        # Let's start by vectorizing the output. We don't know the
        # type though, so it's hard to pick a good factor. Generators
        # provide a helper called "natural_vector_size" which will
        # pick a reasonable factor for you given the type and the
        # target you're compiling to.
        g.output.vectorize(x, g.natural_vector_size(g.output.type()))

        # Now we'll possibly parallelize it:
        if g.parallel:
            g.output.parallel(y)

        # If there was a rotation, we'll schedule that to occur per
        # scanline of the output and vectorize it according to its
        # type.
        if rotation != Rotation.NONE:
            rotated.compute_at(g.output, y).vectorize(
                x, g.natural_vector_size(rotated.types()[0])
            )


if __name__ == "__main__":
    hl.main()
