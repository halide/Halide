#!/usr/bin/python3
#
# Halide tutorial lesson 10: Generators (part 1)
#
# This lesson demonstrates how to encapsulate Halide pipelines into
# reusable components called Generators.
#
# With Halide for Python installed, run
#
#    python3 path/to/lesson_10_generators_1.py
#
# in a shell.
#
# - To install Halide for Python from PyPI:
#   - python3 -m python3 -m pip install halide
#
# - To install Halide for Python from source:
#   - Build and install Halide locally using CMake (see README_cmake.md)
#   - export HALIDE_INSTALL=path/to/halide/install
#   - export PYTHONPATH=$HALIDE_INSTALL/lib/python3/site-packages

import halide as hl
import imageio.v2 as imageio
import numpy as np
import os

# In Halide, a "Generator" is a unit of encapsulation for Halide code.
# It is self-contained piece of code that can:
#
# - Produce a complete Pipeline of Halide IR that is appropriate for compilation
#   using either JIT or AOT
# - Expose itself to build systems in a discoverable way
# - Fully describe itself for build systems, with metadata for
#   (at least) the type and number of inputs and outputs expected
# - Allow for build-time customization of coder-specified parameters
#    in a way that doesn't require editing of source code

# (Note that Halide Generators are completely unrelated to
# Python "Generator" functions (https://wiki.python.org/moin/Generators; in
# most usage, this coincidence of naming doesn't present any ambiguity, but
# in the cases it does, using the term "Halide Generator" should be used
# for clarification. In the rest of this lesson, we'll use "Generator"
# by itself, for terseness.)

# A Generator takes the form of a Python class that is adorned with the
# @hl.generator() decorator; this injects a base class for the class,
# and adds the class to a global registry of available Generators that
# can be used by Python code.
#
# The 'name' argument for the decorator is optional; if present, it is the
# string that will be used to look up this Generator in the registry.
# (If omitted, the class name will be used.)


@hl.generator(name="my_first_generator")
class MyFirstGenerator:
    # We declare the Inputs to the Halide pipeline as public data members
    # that are initialized with values of hl.InputBuffer or hl.InputScalar.
    # They'll appear in the signature of our generated function in the same
    # order that we declare them.
    #
    # Note that InputScalar() is directly analogous to the Param type
    # we've seen before, while InputBuffer is analogous to an ImageParam.
    input_buffer = hl.InputBuffer(hl.UInt(8), 2)
    brighten_factor = hl.InputScalar(hl.Float(32))

    # We also declare the Outputs as public member variables, this time of
    # type hl.OutputScalar or hl.OutputBuffer.
    #
    # Each OutputBuffer is filled in as though it were a Func, with the constraint
    # that it will always be scheduled as .compute_root().
    brighter = hl.OutputBuffer(hl.UInt(8), 2)

    # We then define a method that constructs and schedules a Halide
    # pipeline. Each Generator must define this method.
    def generate(self):
        # We can declare the Vars we use here, or as class scope, or at global
        # scope, depending on what is convenient. We'll just declare some here for now.
        x, y = hl.Var("x"), hl.Var("y")

        # Define the Output(s) as you would for any other Func of Halide code.
        # We'll mimic the code from lesson 2.
        self.brighter[x, y] = hl.cast(
            hl.UInt(8), hl.min(self.input_buffer[x, y] * self.brighten_factor, 255)
        )

        # Also provide whatever schedule you like. As noted above,
        # all Output(s) will implicitly be calculated as .compute_root(),
        # whether you explicitly specify that or not.
        self.brighter.vectorize(x, 16).parallel(y)


def use_first_generator():
    # We can use a Generator with the JIT in several ways; we'll cover just one
    # of them here. (We'll cover using Generators for ahead-of-time compilation in a later lesson.)

    # The simplest way to use a Generator is to simply invoke its .call()
    # method; this allows you to "call" a Generator like an ordinary function.
    #
    # You provide the Inputs to the Generator as the arguments (specified either
    # by-name or by-position in the usual Python way; it returns the Outputs
    # as a Func (or a tuple-of-Funcs if there are multiple Outputs).
    w, h = 640, 480
    input_buffer = hl.Buffer(hl.UInt(8), [w, h])
    input_buffer.fill(128)

    # Using a Generator also requires that there is an active GeneratorContext;
    # this is an object that contains the Target that the Generator will use,
    # along with other information we'll learn about in later lessons.
    # When using a Generator for ahead-of-time compilation, Halide takes
    # care of setting up a GeneratorContext for you, but when using the JIT,
    # you'll need to explicitly activate one, using Python's "with" clause:
    with hl.GeneratorContext(hl.get_jit_target_from_environment()):
        output_func = MyFirstGenerator.call(input_buffer, brighten_factor=1.5)

    # The result is a Func, so we can just realize() it like any other:
    output_buffer = output_func.realize([w, h])

    for yy in range(h):
        for xx in range(w):
            assert output_buffer[xx, yy] == 192


# Let's define another, more complex generator:


@hl.generator(name="my_second_generator")
class MySecondGenerator:
    # This generator will take some compile-time parameters too,
    # known as "GeneratorParams". These let you compile multiple variants
    # of a Halide pipeline. Each GeneratorParam has a default value that can
    # be overridden at compile-time to allow for customization.

    # We'll define one that tells us whether or not to parallelize in our schedule.
    parallel = hl.GeneratorParam(True)

    # You can define GeneratorParams with values of Python's bool, int, float,
    # or str types; you can also use Halide's hl.Type.

    # Here we'll use a string value to emulate an enumerated type;
    # the values we'll accept are "none", "cw", and "ccw".
    rotation = hl.GeneratorParam("none")

    # We'll use the same Inputs as MyFirstGenerator:
    input_buffer = hl.InputBuffer(hl.UInt(8), 2)
    brighten_factor = hl.InputScalar(hl.Float(32))

    # And a similar Output.
    output_buffer = hl.OutputBuffer(hl.UInt(8), 2)

    # We'll declare our Vars at class scope this time.
    # Note that this means we'll have to reference them as self.x and self.y
    # in the generate() method.
    x = hl.Var("x")
    y = hl.Var("y")

    def generate(self):
        # Grab the width and height of the input, for future use
        w = self.input_buffer.width()
        h = self.input_buffer.height()

        # Define the brightening Func.
        brighter = hl.Func("brighter")
        brighter[self.x, self.y] = hl.cast(
            hl.UInt(8),
            hl.min(self.input_buffer[self.x, self.y] * self.brighten_factor, 255),
        )

        # Now, possibly do some sort of rotation, depending on the
        # value specified in self.rotation. (In production code, we'd likely prefer
        # to create a real Enum class for rotation, but for this simple example,
        # we'll just use if-elif.)
        rotated = hl.Func("rotated")
        if self.rotation == "none":
            rotated[self.x, self.y] = brighter[self.x, self.y]
        elif self.rotation == "cw":
            rotated[self.x, self.y] = brighter[self.y, h - self.x - 1]
        elif self.rotation == "ccw":
            rotated[self.x, self.y] = brighter[w - self.y - 1, self.x]
        else:
            # Illegal or unsupported values for a GeneratorParam should always be
            # handled by raising an exception.
            raise ValueError(
                "Unsupported value for GeneratorParam rotation: %s" % self.rotation
            )

        # Finally, assign to the output buffer.
        self.output_buffer[self.x, self.y] = hl.cast(
            self.output_buffer.type(), rotated[self.x, self.y]
        )

        # The structure of the pipeline depended on the generator
        # params, and (in this case) so will the schedule.

        # Let's start by vectorizing the output. We don't know the
        # type though, so it's hard to pick a good factor. Generators
        # provide a helper method called "natural_vector_size" which will
        # pick a reasonable factor for a numeric type, based on the
        # Target you're compiling to.
        v = self.natural_vector_size(self.output_buffer.type())
        self.output_buffer.vectorize(self.x, v)

        # Now we'll possibly parallelize it.
        if self.parallel:
            self.output_buffer.parallel(self.y)

        # If there was a rotation, we'll schedule that to occur per
        # scanline of the output and vectorize it according to its
        # type.
        if self.rotation != "none":
            v = self.natural_vector_size(rotated.type())
            rotated.compute_at(self.output_buffer, self.y).vectorize(self.x, v)


def use_second_generator():
    # We can override values for GeneratorParams by specifying an optional by-name-only argument,
    # generator_params, to the .call() method. It takes a simple Python dict as its value.

    # We'll read in an image here so that the effects of the code are easier to see.

    image_path = os.path.join(os.path.dirname(__file__), "images/gray.png")
    input_buffer = hl.Buffer(imageio.imread(image_path))

    # Now, let's override rotation to specify a clockwise rotate.
    rotate_cw = {
        "rotation": "cw",
    }

    with hl.GeneratorContext(hl.get_jit_target_from_environment()):
        output_func = MySecondGenerator.call(
            input_buffer, brighten_factor=1.5, generator_params=rotate_cw
        )

    # Since we're rotating it, we must adjust the output buffer dimensions
    # appropriately (note that height and width are swapped!)
    w, h = input_buffer.width(), input_buffer.height()
    output_buffer = output_func.realize([h, w])

    imageio.imsave("rotated.png", output_buffer)
    print("Created rotated.png result file.")


if __name__ == "__main__":
    use_first_generator()
    use_second_generator()
